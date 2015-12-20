// //
//  Драйверы для работы с Flash модема через обращения к NAND-контроллеру и процедурам загрузчика
//
#include "include.h"

// Глбальные переменные - собираем их здесь

unsigned int nand_cmd=0x1b400000;
unsigned int spp=0;
unsigned int pagesize=0;
unsigned int sectorsize=512;
unsigned int maxblock=0;     // Общее число блоков флешки
char flash_mfr[30]={0};
char flash_descr[30]={0};
unsigned int oobsize=0;
unsigned int bad_loader=0;
unsigned int flash16bit=0; // 0 - 8-битная флешка, 1 - 16-битная

unsigned int badsector;    // сектор, содержащий дефектный блок
unsigned int badflag;      // маркер дефектного блока
unsigned int badposition;  // позиция маркера дефектных блоков

//****************************************************************
//* Ожидание завершения операции, выполняемой контроллером nand  *
//****************************************************************
void nandwait() { 
  if (get_controller() == 0) 
    while ((mempeek(nand_status)&0xf) != 0);  // MDM
  else
    while ((mempeek(nand_status)&0x3) != 0);  // MSM
}



//*************************************88
//* Установка адресных регистров 
//*************************************
void setaddr(int block, int page) {

int adr;  
  
adr=block*ppb+page;  // # страницы от начала флешки

if (get_controller() == 0) {
  // MDM
  mempoke(nand_addr0,adr<<16);         // младшая часть адреса. 16 бит column address равны 0
  mempoke(nand_addr1,(adr>>16)&0xff);  // единственный байт старшей части адреса
}  
else {
  // MSM
  mempoke(nand_addr0,adr<<8);
}
}

//***************************************************************
//* Запуск на выполнение команды контроллера NAND с ожиданием
//***************************************************************
void exec_nand(int cmd) {

if (get_controller() == 0) {
  // MDM  
  mempoke(nand_cmd,cmd); // Сброс всех операций контроллера
  mempoke(nand_exec,0x1);
  nandwait();
}
else {
  // MSM
  mempoke(nand_cmd,cmd); // Сброс всех операций контроллера
  nandwait();
}
}



//*********************************************
//* Сброс контроллера NAND
//*********************************************
void nand_reset() {

exec_nand(1);  
}

//*********************************************
//* Чтение сектора флешки по указанному адресу 
//*********************************************

int flash_read(int block, int page, int sect) {
  
int i;

nand_reset();
// адрес
setaddr(block,page);
if (get_controller() == 0) {
  // MDM - устанавливаем код команды один раз
  mempoke(nand_cmd,0x34); // чтение data+ecc+spare
  // цикл чтения сектров до нужного нам
  for(i=0;i<(sect+1);i++) {
    mempoke(nand_exec,0x1);
    nandwait();
  }  
}
else {
  // MSM - код команды в регистр команд вводится каждый раз
  for(i=0;i<(sect+1);i++) {
    mempoke(nand_cmd,0x34); // чтение data+ecc+spare
    nandwait();
  }  
}  
if (test_badblock()) return 0;
return 1;  
}

//**********************************************8
//* Процедура активации загрузчика hello
//*
//* mode=0 - автоопределение нужности hello
//* mode=1 - принудительный запуск hello
//* mode=2 - принудительный запуск hello без настройки конфигурации 
//**********************************************8
void hello(int mode) {

int i;  
unsigned char rbuf[1024];
char hellocmd[]="\x01QCOM fast download protocol host\x03### ";

int bch_mode=0;

// апплет проверки работоспособности загрузчика
unsigned char cmdbuf[]={
  0x11,0x00,0x12,0x00,0xa0,0xe3,0x00,0x00,
  0xc1,0xe5,0x01,0x40,0xa0,0xe3,0x1e,0xff,
  0x2f,0xe1
};
unsigned int cfg0,cfg1,ecccfg;

// режим тихой инициализации
if (mode == 0) {
  i=send_cmd(cmdbuf,sizeof(cmdbuf),rbuf);
  ttyflush(); 
  i=rbuf[1];
  // Проверяем, не инициализировался ли загрузчик ранее
  if (i == 0x12) {
     if (!test_loader()) {
       printf("\n Используется непатченный загрузчик - продолжение работы невозможно\n");
        exit(1);
     }  
//     printf("\n chipset = %i  base = %i",chip_type,name);
     get_flash_config();
     return;
  }  
  read(siofd,rbuf,1024);   // вычищаем хвост буера с сообщением об ошибке
}  

printf(" Отсылка hello...");
i=send_cmd(hellocmd,strlen(hellocmd),rbuf);
if (rbuf[1] != 2) {
   i=send_cmd(hellocmd,strlen(hellocmd),rbuf);
   if (rbuf[1] != 2) {
     printf(" повторный hello возвратил ошибку!\n");
     dump(rbuf,i,0);
     return;
   }  
}  
i=rbuf[0x2c];
rbuf[0x2d+i]=0;
printf("ok");
if (mode == 2) {
   // тихий запуск - обходим настройку чипсета
   printf(", флеш-память: %s\n",rbuf+0x2d);
   return; 
 }  
ttyflush(); 
if (!test_loader()) {
  printf("\n Используется непатченный загрузчик - продолжение работы невозможно\n");
  exit(1);
}  


printf("\n Чипсет: %s  (%08x)",get_chipname(),nand_cmd); fflush(stdout);
if (get_sahara()) disable_bam(); // отключаем NANDc BAM, если работаем с чипсетами нового поколения

cfg0=mempeek(nand_cfg0);
cfg1=mempeek(nand_cfg1);
if (nand_ecc_cfg != 0xffff) ecccfg=mempeek(nand_ecc_cfg);
else ecccfg=0;
get_flash_config();
printf("\n Флеш-память: %s %s, %s",flash_mfr,(rbuf[0x2d] != 0x65)?((char*)(rbuf+0x2d)):"",flash_descr); fflush(stdout);
printf("\n Версия протокола: %i",rbuf[0x22]); fflush(stdout);
//printf("\n Максимальный размер пакета: %i байта",*((unsigned int*)&rbuf[0x24]));fflush(stdout);
printf("\n Размер сектора: %u байт",sectorsize);fflush(stdout);
printf("\n Размер страницы: %u байт (%u секторов)",pagesize,spp);fflush(stdout);
printf("\n Размер OOB: %u байт",oobsize); fflush(stdout);

// Определяем тип ЕСС
if (((cfg1>>27)&1) != 0) bch_mode=1;

if (bch_mode) {
  printf("\n Тип ECC: BCH, %i бит",((ecccfg>>4)&3) ? (((ecccfg>>4)&3)+1)*4 : 4);
  printf("\n Размер ЕСС: %u байт",(ecccfg>>8)&0x1f);fflush(stdout);
}  
else {
  printf("\n Тип ECC: R-S, 4 бит");
  printf("\n Размер ЕСС: %u байт",(cfg0>>19)&0xf);
} 
  
printf("\n Размер spare: %u байт",(cfg0>>23)&0xf);

printf("\n Положение маркера дефектных блоков: ");
badposition=(cfg1>>6)&0x3ff;
if (badposition == 0) printf(" маркер отсутствует");
else {
  if (((cfg1>>16)&1) == 0) printf("user");
  else printf("spare");
  printf("+%x",badposition);
}

printf("\n Общий размер флеш-памяти = %u блоков (%i MB)",maxblock,maxblock*ppb/1024*pagesize/1024);fflush(stdout);
printf("\n");
}

//**********************************************
//* Отключение аппаратного контроля бедблоков
//**********************************************
void hardware_bad_off() {

int cfg1;

cfg1=mempeek(nand_cfg1);
cfg1 &= ~(0x3ff<<6);
mempoke(nand_cfg1,cfg1);
}

//**********************************************
//* Включение аппаратного контроля бедблоков
//**********************************************
void hardware_bad_on() {

int cfg1;

cfg1=mempeek(nand_cfg1);
cfg1 &= ~(0x3ff<<6);
cfg1 |= (badposition <<6)&0x3ff;
mempoke(nand_cfg1,cfg1);
}




//*************************************
//* чтение таблицы разделов из flash
//*************************************
void load_ptable(unsigned char* buf) {

unsigned int udsize=512;
unsigned int blk,pg;

if (get_udflag()) udsize=516;

memset(buf,0,1024); // обнуляем таблицу

for (blk=0;blk<12;blk++) {
  // Ищем блок с картами
  flash_read(blk, 0, 0);     
  memread(buf,sector_buf, udsize);
  if (memcmp(buf,"\xac\x9f\x56\xfe\x7a\x12\x7f\xcd",8) != 0) continue; // сигнатура не найдена - ищем дальше

  // нашли блок с таблицами - теперь ищем страницу с таблицей чтения
  for (pg=0;pg<ppb;pg++) {
    flash_read(blk, pg, 0);     
    memread(buf,sector_buf, udsize);
    if (memcmp(buf,"\xAA\x73\xEE\x55\xDB\xBD\x5E\xE3",8) != 0) continue; // сигнатура таблицы чтения не найдена
    // нашли таблицу - читаем хвост
    mempoke(nand_exec,1);     // сектор 1 - продолжение таблицы
    nandwait();
    memread(buf+udsize,sector_buf, udsize);
    return; // все - таблица найдна, более тут делать нечего
  }
}  
printf("\n Таблица разделов не найдена. Завершаем работу.");
exit(1);
}

//**********************************
//* Закрытие потока данных раздела
//**********************************
int qclose(int errmode) {
unsigned char iobuf[600];
unsigned char cmdbuf[]={0x15};
int iolen;

iolen=send_cmd(cmdbuf,1,iobuf);
if (!errmode) return 1;
if (iobuf[1] == 0x16) return 1;
show_errpacket("close()",iobuf,iolen);
return 0;

}  

//************************
//* Стирание блока флешки  
//************************

void block_erase(int block) {
  
int oldcfg;  
  
mempoke(nand_addr0,block*ppb);         // младшая часть адреса - # страницы
mempoke(nand_addr1,0);                 // старшая часть адреса - всегда 0

oldcfg=mempeek(nand_cfg0);
mempoke(nand_cfg0,oldcfg&~(0x1c0));    // устанавливаем CW_PER_PAGE=0, как требует даташит

mempoke(nand_cmd,0x3a); // стирание. Бит Last page установлен
mempoke(nand_exec,0x1);
nandwait();
mempoke(nand_cfg0,oldcfg);   // восстанавливаем CFG0
}

//**********************************************************
//*  Получение параметров формата флешки из контроллера
//**********************************************************
void get_flash_config() {
  
unsigned int cfg0, cfg1, nandid, pid, fid, blocksize, devcfg, chipsize;
int i;

struct {
  char* type;   // тектовое описание типа
  unsigned int id;      // ID флешки 
  unsigned int chipsize; // размер флешки в мегабайтах
} nand_ids[]= {

	{"NAND 16MiB 1,8V 8-bit",	0x33, 16},
	{"NAND 16MiB 3,3V 8-bit",	0x73, 16}, 
	{"NAND 16MiB 1,8V 16-bit",	0x43, 16}, 
	{"NAND 16MiB 3,3V 16-bit",	0x53, 16}, 

	{"NAND 32MiB 1,8V 8-bit",	0x35, 32},
	{"NAND 32MiB 3,3V 8-bit",	0x75, 32},
	{"NAND 32MiB 1,8V 16-bit",	0x45, 32},
	{"NAND 32MiB 3,3V 16-bit",	0x55, 32},

	{"NAND 64MiB 1,8V 8-bit",	0x36, 64},
	{"NAND 64MiB 3,3V 8-bit",	0x76, 64},
	{"NAND 64MiB 1,8V 16-bit",	0x46, 64},
	{"NAND 64MiB 3,3V 16-bit",	0x56, 64},

	{"NAND 128MiB 1,8V 8-bit",	0x78, 128},
	{"NAND 128MiB 1,8V 8-bit",	0x39, 128},
	{"NAND 128MiB 3,3V 8-bit",	0x79, 128},
	{"NAND 128MiB 1,8V 16-bit",	0x72, 128},
	{"NAND 128MiB 1,8V 16-bit",	0x49, 128},
	{"NAND 128MiB 3,3V 16-bit",	0x74, 128},
	{"NAND 128MiB 3,3V 16-bit",	0x59, 128},

	{"NAND 256MiB 3,3V 8-bit",	0x71, 256},

	/*512 Megabit */
	{"NAND 64MiB 1,8V 8-bit",	0xA2, 64},   
	{"NAND 64MiB 1,8V 8-bit",	0xA0, 64},
	{"NAND 64MiB 3,3V 8-bit",	0xF2, 64},
	{"NAND 64MiB 3,3V 8-bit",	0xD0, 64},
	{"NAND 64MiB 1,8V 16-bit",	0xB2, 64},
	{"NAND 64MiB 1,8V 16-bit",	0xB0, 64},
	{"NAND 64MiB 3,3V 16-bit",	0xC2, 64},
	{"NAND 64MiB 3,3V 16-bit",	0xC0, 64},

	/* 1 Gigabit */
	{"NAND 128MiB 1,8V 8-bit",	0xA1,128},
	{"NAND 128MiB 3,3V 8-bit",	0xF1,128},
	{"NAND 128MiB 3,3V 8-bit",	0xD1,128},
	{"NAND 128MiB 1,8V 16-bit",	0xB1,128},
	{"NAND 128MiB 3,3V 16-bit",	0xC1,128},
	{"NAND 128MiB 1,8V 16-bit",     0xAD,128},

	/* 2 Gigabit */
	{"NAND 256MiB 1.8V 8-bit",	0xAA,256},
	{"NAND 256MiB 3.3V 8-bit",	0xDA,256},
	{"NAND 256MiB 1.8V 16-bit",	0xBA,256},
	{"NAND 256MiB 3.3V 16-bit",	0xCA,256},

	/* 4 Gigabit */
	{"NAND 512MiB 1.8V 8-bit",	0xAC,512},
	{"NAND 512MiB 3.3V 8-bit",	0xDC,512},
	{"NAND 512MiB 1.8V 16-bit",	0xBC,512},
	{"NAND 512MiB 3.3V 16-bit",	0xCC,512},

	/* 8 Gigabit */
	{"NAND 1GiB 1.8V 8-bit",	0xA3,1024},
	{"NAND 1GiB 3.3V 8-bit",	0xD3,1024},
	{"NAND 1GiB 1.8V 16-bit",	0xB3,1024},
	{"NAND 1GiB 3.3V 16-bit",	0xC3,1024},

	/* 16 Gigabit */
	{"NAND 2GiB 1.8V 8-bit",	0xA5,2048},
	{"NAND 2GiB 3.3V 8-bit",	0xD5,2048},
	{"NAND 2GiB 1.8V 16-bit",	0xB5,2048},
	{"NAND 2GiB 3.3V 16-bit",	0xC5,2048},

	/* 32 Gigabit */
	{"NAND 4GiB 1.8V 8-bit",	0xA7,4096},
	{"NAND 4GiB 3.3V 8-bit",	0xD7,4096},
	{"NAND 4GiB 1.8V 16-bit",	0xB7,4096},
	{"NAND 4GiB 3.3V 16-bit",	0xC7,4096},

	/* 64 Gigabit */
	{"NAND 8GiB 1.8V 8-bit",	0xAE,8192},
	{"NAND 8GiB 3.3V 8-bit",	0xDE,8192},
	{"NAND 8GiB 1.8V 16-bit",	0xBE,8192},
	{"NAND 8GiB 3.3V 16-bit",	0xCE,8192},

	/* 128 Gigabit */
	{"NAND 16GiB 1.8V 8-bit",	0x1A,16384},
	{"NAND 16GiB 3.3V 8-bit",	0x3A,16384},
	{"NAND 16GiB 1.8V 16-bit",	0x2A,16384},
	{"NAND 16GiB 3.3V 16-bit",	0x4A,16384},
                                                  
	/* 256 Gigabit */
	{"NAND 32GiB 1.8V 8-bit",	0x1C,32768},
	{"NAND 32GiB 3.3V 8-bit",	0x3C,32768},
	{"NAND 32GiB 1.8V 16-bit",	0x2C,32768},
	{"NAND 32GiB 3.3V 16-bit",	0x4C,32768},

	/* 512 Gigabit */
	{"NAND 64GiB 1.8V 8-bit",	0x1E,65536},
	{"NAND 64GiB 3.3V 8-bit",	0x3E,65536},
	{"NAND 64GiB 1.8V 16-bit",	0x2E,65536},
	{"NAND 64GiB 3.3V 16-bit",	0x4E,65536},
	{0,0,0},
};


struct  {
  unsigned int id;
  char* name;
}  nand_manuf_ids[] = {
	{0x98, "Toshiba"},
	{0xec, "Samsung"},
	{0x04, "Fujitsu"},
	{0x8f, "National"},
	{0x07, "Renesas"},
	{0x20, "ST Micro"},
	{0xad, "Hynix"},
	{0x2c, "Micron"},
	{0xc8, "Elite Semiconductor"},
	{0x01, "Spansion/AMD"},
	{0x0, 0}
};

mempoke(nand_cmd,0x8000b); // команда Extended Fetch ID
mempoke(nand_exec,1); 
nandwait();
nandid=mempeek(NAND_FLASH_READ_ID); // получаем ID флешки
chipsize=0;

fid=(nandid>>8)&0xff;
pid=nandid&0xff;

// Определяем производителя флешки
i=0;
while (nand_manuf_ids[i].id != 0) {
	if (nand_manuf_ids[i].id == pid) {
	strcpy(flash_mfr,nand_manuf_ids[i].name);
	break;
	}
i++;
}  
    
// Определяем емкость флешки
i=0;
while (nand_ids[i].id != 0) {
if (nand_ids[i].id == fid) {
	chipsize=nand_ids[i].chipsize;
	strcpy(flash_descr,nand_ids[i].type);
	break;
	}
i++;
}  
if (chipsize == 0) {
	printf("\n Неопределенный Flash ID = %02x",fid);
}  

// Вынимаем параметры конфигурации

sectorsize=512;
devcfg = (nandid>>24) & 0xff;
pagesize = 1024 << (devcfg & 0x3); // размер страницы в байтах
blocksize = 64 << ((devcfg >> 4) & 0x3);  // размер блока в килобайтах
spp = pagesize/sectorsize; // секторов в странице

cfg0=mempeek(nand_cfg0);
if ((((cfg0>>6)&7)|((cfg0>>2)&8)) == 0) {
  // для старых чипсетов младшие 2 байта CFG0 надо настраивать руками
  if (!bad_loader) mempoke(nand_cfg0,(cfg0|0x40000|(((spp-1)&8)<<2)|(((spp-1)&7)<<6)));
}  

// Для чипсета 8200 требуется настройка конфигурации 1 - позиции маркера бедблока
cfg1=mempeek(nand_cfg1);
if (nand_cmd == 0xa0a00000) {
  cfg1&=~(0x7ff<<6);  //сбрасываем BAD_BLOCK_BYTE_NUM и BAD_BLOCK_IN_SPARE_AREA
  cfg1|=(0x1d1<<6); // BAD_BLOCK_BYTE_NUM
  mempoke(nand_cfg1,cfg1);
}  

// проверяем признак 16-битной флешки
if ((cfg1&2) != 0) flash16bit=1;
if (chipsize != 0)   maxblock=chipsize*1024/blocksize;
else                 maxblock=0x800;

if (oobsize == 0) {
	// Micron MT29F4G08ABBEA3W: на самом деле 224, определяется 128, 
	// реально используется 160, для raw-режима нагляднее 256 :)
	if (nandid == 0x2690ac2c) oobsize = 256; 
	else oobsize = (8 << ((devcfg >> 2) & 0x1)) * (pagesize >> 9);
} 

}

//****************************************
//* Отключение NANDc BAM
//****************************************
void disable_bam() {

unsigned int i,nandcstate[256];

for (i=0;i<0xec;i+=4) nandcstate[i]=mempeek(nand_cmd+i); // сохраняем состояние контроллера NAND
mempoke(0xfc401a40,1); // GCC_QPIC_BCR
mempoke(0xfc401a40,0); // полный асинхронный сброс QPIC
for (i=0;i<0xec;i+=4) mempoke(nand_cmd+i,nandcstate[i]);  // восстанавливаем состояние
mempoke(nand_exec,1); // фиктивное чтение для снятия защиты адресных регистров контроллера от записи
}


//****************************************************
//* Проверка массива на ненулевые значения
//*
//*  0 - в массиве одни нули
//*  1 - в массиве есть ненули
//****************************************************
int test_zero(unsigned char* buf, int len) {
  
int i;
for (i=0;i<len;i++)
  if (buf[i] != 0) return 1;
return 0;
}

//***************************************************************
//* Идентификация чипсета через апплет по сигнатуре загрузчика
//*
//* return -1 - загрузчик не поддерживает команду 11
//*         0 - в загрузчике не найдена сигнатура идентификации чипсета
//*         остальное - код чипсета из загрузчика 
//***************************************************************
int identify_chipset() {

char cmdbuf[]={ 
  0x11,0x00,0x04,0x10,0x2d,0xe5,0x0e,0x00,0xa0,0xe1,0x03,0x00,0xc0,0xe3,0xff,0x30,
  0x80,0xe2,0x34,0x10,0x9f,0xe5,0x04,0x20,0x90,0xe4,0x01,0x00,0x52,0xe1,0x03,0x00,
  0x00,0x0a,0x03,0x00,0x50,0xe1,0xfa,0xff,0xff,0x3a,0x00,0x00,0xa0,0xe3,0x00,0x00,
  0x00,0xea,0x00,0x00,0x90,0xe5,0x04,0x10,0x9d,0xe4,0x01,0x00,0xc1,0xe5,0xaa,0x00,
  0xa0,0xe3,0x00,0x00,0xc1,0xe5,0x02,0x40,0xa0,0xe3,0x1e,0xff,0x2f,0xe1,0xef,0xbe,
  0xad,0xde
};
unsigned char iobuf[1024];
int iolen;
iolen=send_cmd(cmdbuf,sizeof(cmdbuf),iobuf);
//printf("\n--ident--\n");
//dump(iobuf,iolen,0);
if (iobuf[1] != 0xaa) return -1;
return iobuf[2];
}

//*******************************************************
//* Проверка работы патча загрузчика
//*
//* Возвращает 0, если команда 11 не поддерживается
//* и устанавливает глобальную переменную bad_loader=1
//*******************************************************
int test_loader() {

int i;

i=identify_chipset();
//printf("\n ident = %i\n",i);
if (i<=0) {
  bad_loader=1;
  return 0;
}
if (chip_type == 0) set_chipset(i); // если чипсет не был явно задан
return 1;
}

//****************************************************************
//*  Проверка флага дефектного блока предыдущей операции чтения 
//*
//* 0 -нет бедблока
//* 1 -есть
//****************************************************************

int test_badblock() {

unsigned int st,r,badflag=0;

// Старшие 2 байта регистра nand_buffer_status отражают прочитанный с флешки маркер. 
// Для 8-битных флешек  используется только младший байт, для 16-битных - оба байта
st=r=mempeek(nand_buffer_status)&0xffff0000;
if (flash16bit == 0) {
  if (st != 0xff0000) { 
    badflag=1;  
//     printf("\nst=%08x",r);    
  }
}  
else  if (st != 0xffff0000) badflag=1;
return badflag;
}


  