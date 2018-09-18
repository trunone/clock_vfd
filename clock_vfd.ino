// Original source code from http://www.junkyard.jp/labo/vfd_clock/rev230/index.html

#include "RTC8564.h"
#include <TimerOne.h>
#include <inttypes.h>
#include <ModbusMaster.h>

const unsigned char ver[] = "07a";
#define  TIMER1_INTTIME  500     // タイマインタラプト周期
#define  COLON_PWM      0x20     // : 点灯用PWM高さ
#define  COLON_BRIGHT   0x04     // : の明るさ。値が大きいほど明るくなる。
#define  DISP_PRE      (2000/TIMER1_INTTIME)  // 数字表示周期作成
//#define  RTC_TEST                // RTC動作テスト
//#define  KEY_TEST                // キー入力テスト表示 Revision210以前はテスト不要

unsigned int count;
unsigned int second_counterw;
unsigned int colon_brightw;
unsigned char time[6];          // 数値表示データ
unsigned char piriod[6];        // 各桁ピリオドデータ
unsigned char font[18] = {
  /*  cebagfdp  */
  0b11110110, //0
  0b10100000, //1
  0b01111010, //2
  0b10111010, //3
  0b10101100, //4
  0b10011110, //5
  0b11011110, //6
  0b10110100, //7
  0b11111110, //8
  0b10111110, //9
  0b11111100, //A
  0b11001110, //B
  0b01001010, //C
  0b11101010, //D
  0b01011110, //E
  0b01011100, //F
  0b00000000, //NON_DISP
  0b11111111
};
#define DISP_NON  16
#define DISP_FULL 17

// VFD点灯順行列
// 以下の行列の順にVFDは点灯する。
// 特定の桁が暗かったり明るかったりする場合は、この行列で点灯頻度を変える事で調整可能
// 明るいVFDの番号を少なく、暗いVFDの番号を多くする。
// VFD番号の順序はあまり気にしなくても大丈夫
unsigned char keta[] = {
  1, 2, 3, 4, 5, 6,
  1, 2, 3, 4, 5, 6,
  1, 2, 3, 4, 5, 6,
  1, 2, 3, 4, 5, 6
};
#define DISP_KETAMAX sizeof(keta)

unsigned char mode;
#define MODE_CLOCK           0
#define MODE_ADJ_HOUR_WAIT   1
#define MODE_ADJ_HOUR        2
#define MODE_ADJ_MIN_WAIT    3
#define MODE_ADJ_MIN         4
#define MODE_DATE            5
#define MODE_ADJ_YEAR_WAIT   6
#define MODE_ADJ_YEAR        7
#define MODE_ADJ_MONTH_WAIT  8
#define MODE_ADJ_MONTH       9
#define MODE_ADJ_DATE_WAIT  10
#define MODE_ADJ_DATE       11
#define MODE_RTC_TEST       12
#define MODE_CLOCK_SYNC     13

// SW定義
#define BTN1 14
#define BTN2 15
#define BTN3 16

unsigned int itm_firstf;
unsigned char BTN1_chkw;
unsigned char BTN2_chkw;
unsigned char BTN3_chkw;

#define ITM_ANACH   0
#define ITM_SW123   0
#define ITM_SW12    1
#define ITM_SW13    2
#define ITM_SW1     3
#define ITM_SW23    4
#define ITM_SW2     5
#define ITM_SW3     6
#define ITM_SW_NON  7
static unsigned int itm_base_boundary[] = {0, 187, 200, 213, 236, 262, 286, 315, 500};
unsigned int itm_boundary[sizeof(itm_base_boundary)];

unsigned char swtest_1[] = { 1, 1, 1, 1, 0, 0, 0, 0 };
unsigned char swtest_2[] = { 1, 1, 0, 0, 1, 1, 0, 0 };
unsigned char swtest_3[] = { 1, 0, 1, 0, 1, 0, 1, 0 };


uint8_t last_second;
uint8_t date_time[7];

unsigned char key_now;
unsigned char itm_key_sq;
unsigned char itm_key_num;

//
void adj(unsigned char key);
void disp_datamake(void);
void itm_man(void);
void itm(void);
void itm_ana_ini(void);
unsigned char itm_ana_tmread(void);
unsigned char itm_ana(unsigned char key);
void disp_vfd(void);
void disp_colon(unsigned char disp_count, unsigned char *portc, unsigned char *portd);
void disp_colon_leonardo(unsigned char disp_count, unsigned char *portf);
void int_count(void);
void rtc_chk(void);

RTC8564 Rtc;
ModbusMaster node;

void setup()
{
  int i;

  for (i = 0; i <= 13; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i, LOW);
  }
  pinMode(17, OUTPUT);      // -
  digitalWrite(17, LOW);

  //  入力スイッチポート初期化
  pinMode(14, INPUT);
  itm_ana_ini();

  pinMode(A4, OUTPUT);      // :
  digitalWrite(A4, LOW);

  BTN1_chkw = 0;
  BTN2_chkw = 0;
  BTN3_chkw = 0;
  itm_firstf = 0;

  Rtc.begin();

  // タイマインタラプト初期化
  Timer1.initialize(TIMER1_INTTIME);      // 割り込み周期設定
  Timer1.attachInterrupt(int_count);

  Serial.begin(9600);
#ifdef RTC_TEST
  mode = MODE_RTC_TEST;
#else
  mode = MODE_CLOCK;
#endif

  second_counterw = int(1000000 / TIMER1_INTTIME);  // 1秒間の割り込み回数
  count = 0;
  last_second = 0;

  node.begin(1, Serial);
}

void loop()
{
  rtc_chk();        // RTC読み出し
  itm_man();        // SW入力処理
  disp_datamake();  // 表示データ作成
}


void sirial_out(void) {
  if ( (mode == MODE_RTC_TEST)) {
    Serial.print(0x2000 + date_time[6], HEX);
    Serial.print("/");
    Serial.print(date_time[5], HEX);
    Serial.print("/");
    Serial.print(date_time[3], HEX);
    Serial.print(" ");
    Serial.print(date_time[2], HEX);
    Serial.print(":");
    Serial.print(date_time[1], HEX);
    Serial.print(":");
    Serial.println(date_time[0], HEX);

    Serial.print("SW1:");
    Serial.print(swtest_1[key_now], DEC);
    Serial.print(" SW2:");
    Serial.print(swtest_2[key_now], DEC);
    Serial.print(" SW3:");
    Serial.println(swtest_3[key_now], DEC);
  }
  return;
}

void rtc_chk(void) {
  uint8_t now_second;

  Rtc.available();
  date_time[0] = Rtc.seconds();
  if (mode == MODE_CLOCK) {
    date_time[1] = Rtc.minutes();
    date_time[2] = Rtc.hours();
  }
  else if (mode == MODE_DATE) {
    date_time[3] = Rtc.days();
    date_time[4] = Rtc.weekdays();
    date_time[5] = Rtc.months();
    date_time[6] = Rtc.years();
  }
  else if (mode == MODE_RTC_TEST) {
    date_time[1] = Rtc.minutes();
    date_time[2] = Rtc.hours();
    date_time[3] = Rtc.days();
    date_time[4] = Rtc.weekdays();
    date_time[5] = Rtc.months();
    date_time[6] = Rtc.years();
  }

  return;
}

void disp_datamake(void) {
  unsigned int i;
  unsigned char time_tmp[6];

#ifdef KEY_TEST
  time_tmp[0] = key_now;
  time_tmp[1] = DISP_NON;
  time_tmp[2] = DISP_NON;
  time_tmp[3] = DISP_NON;
  time_tmp[4] = DISP_NON;
  time_tmp[5] = DISP_NON;
#else
  // 時刻情報作成
  if (mode < MODE_DATE) {
    time_tmp[0] = date_time[0] & 0x0F;
    time_tmp[1] = date_time[0] / 0x10;
    time_tmp[2] = date_time[1]  & 0x0F;
    time_tmp[3] = date_time[1] / 0x10;
    time_tmp[4] = date_time[2]  & 0x0F;
    time_tmp[5] = date_time[2] / 0x10;
    colon_brightw = COLON_BRIGHT;
  }
  else {
    time_tmp[0] = date_time[3] & 0x0F;
    time_tmp[1] = date_time[3] / 0x10;
    time_tmp[2] = date_time[5]  & 0x0F;
    time_tmp[3] = date_time[5] / 0x10;
    time_tmp[4] = date_time[6]  & 0x0F;
    time_tmp[5] = date_time[6] / 0x10;
    colon_brightw = 0;
  }
#endif

  // 点滅処理
  if (count >= (second_counterw / 2)) {
    // ピリオド消灯処理
    piriod[0] = 0x00;
    piriod[2] = 0x00;
    piriod[4] = 0x00;
  }
  else {
    // ピリオド点灯処理
    piriod[0] = 0x01;
    piriod[2] = 0x01;
    piriod[4] = 0x01;

    // 時計あわせ時桁点滅処理
    if ( (mode == MODE_ADJ_HOUR_WAIT) || (mode == MODE_ADJ_HOUR)
         || (mode == MODE_ADJ_YEAR_WAIT) || (mode == MODE_ADJ_YEAR) ) {
      time_tmp[4] = DISP_NON;
      time_tmp[5] = DISP_NON;
    }
    else if ((mode == MODE_ADJ_MIN_WAIT) || (mode == MODE_ADJ_MIN)
             || (mode == MODE_ADJ_MONTH_WAIT) || (mode == MODE_ADJ_MONTH)) {
      time_tmp[2] = DISP_NON;
      time_tmp[3] = DISP_NON;
    }
    else if ((mode == MODE_ADJ_DATE_WAIT) || (mode == MODE_ADJ_DATE)) {
      time_tmp[0] = DISP_NON;
      time_tmp[1] = DISP_NON;
    }
  }

  if ( (mode == MODE_DATE)
       || (mode == MODE_ADJ_YEAR_WAIT) || (mode == MODE_ADJ_YEAR)
       || (mode == MODE_ADJ_MONTH_WAIT) || (mode == MODE_ADJ_MONTH)
       || (mode == MODE_ADJ_DATE_WAIT) || (mode == MODE_ADJ_DATE) ) {
    piriod[0] = 0x01;
    piriod[2] = 0x01;
    piriod[4] = 0x01;
  }

  noInterrupts();      // 割り込み禁止
  for (i = 0; i < 6; i++) {
    time[i] = time_tmp[i];
  }
  interrupts();        // 割り込み許可

  return;
}

void adj(unsigned char key) {
#ifdef KEY_TEST
  return;
#endif

  switch (mode) {
    case MODE_CLOCK:
      if (key == BTN1) {
        mode = MODE_ADJ_HOUR_WAIT;
      }
      else if (key == BTN2) {
        mode = MODE_DATE;
      }
      else if (key == BTN3) {
        uint8_t result = node.readHoldingRegisters(0x1000, 7);
        if (result == node.ku8MBSuccess)
        {
          for (int i = 0; i < 7; i++)
          {
            date_time[i] = node.getResponseBuffer(i);
            date_time[i] = (date_time[i]/10)*16 + (date_time[i]%10);
          }
        }
        Rtc.sync(date_time, 7);
      }
      break;
    case MODE_DATE:
      if (key == BTN1) {
        mode = MODE_ADJ_YEAR_WAIT;
      }
      else if ((key == BTN2) || (key == BTN3)) {
        mode = MODE_CLOCK;
      }
      break;

    case MODE_ADJ_YEAR_WAIT:
      if (key == BTN1) {
        mode = MODE_ADJ_MONTH_WAIT;
        break;
      }
      else if ((key == BTN2) || (key == BTN3)) {
        mode = MODE_ADJ_YEAR;  // 時刻調整はbreakせずにこのままMODE_ADJ_YEARに行って行う。
      }
      else {
        break;
      }
    case MODE_ADJ_YEAR:
      if (key == BTN1) {
        mode = MODE_ADJ_MONTH_WAIT;
      }
      else if (key == BTN2) {
        if (date_time[6] == 0x99) {
          date_time[6] = 0x00;
        }
        else if ((date_time[6] & 0x0F) == 0x09) {
          date_time[6] += 0x07;
        }
        else {
          date_time[6]++;
        }
      }
      else if (key == BTN3) {
        if (date_time[6] == 0x00) {
          date_time[6] = 0x99;
        }
        else if ((date_time[6] & 0x0F) == 0x00) {
          date_time[6] -= 0x07;
        }
        else {
          date_time[6]--;
        }
      }
      break;
    case MODE_ADJ_MONTH_WAIT:
      if (key == BTN1) {
        mode = MODE_ADJ_DATE_WAIT;
        break;
      }
      else if ((key == BTN2) || (key == BTN3)) {
        mode = MODE_ADJ_MONTH;  // 時刻調整はbreakせずにこのままMODE_ADJ_YEARに行って行う。
      }
      else {
        break;
      }
    case MODE_ADJ_MONTH:
      if (key == BTN1) {
        mode = MODE_ADJ_DATE_WAIT;
      }
      else if (key == BTN2) {
        if (date_time[5] == 0x12) {
          date_time[5] = 0x01;
        }
        else if ((date_time[5] & 0x0F) == 0x09) {
          date_time[5] += 0x07;
        }
        else {
          date_time[5]++;
        }
      }
      else if (key == BTN3) {
        if (date_time[5] == 0x00) {
          date_time[5] = 0x12;
        }
        else if ((date_time[5] & 0x0F) == 0x00) {
          date_time[5] -= 0x07;
        }
        else {
          date_time[5]--;
        }
      }
      break;

    case MODE_ADJ_DATE_WAIT:
      if (key == BTN1) {
        mode = MODE_DATE;
        Rtc.sync(date_time, 7);
        break;
      }
      else if ((key == BTN2) || (key == BTN3)) {
        mode = MODE_ADJ_DATE;  // 時刻調整はbreakせずにこのままMODE_ADJ_DATEに行って行う。
      }
      else {
        break;
      }
    case MODE_ADJ_DATE:
      if (key == BTN1) {
        mode = MODE_DATE;
        Rtc.sync(date_time, 7);
      }
      else if (key == BTN2) {
        if (date_time[3] == 0x31) {
          date_time[3] = 0x01;
        }
        else if ((date_time[3] & 0x0F) == 0x09) {
          date_time[3] += 0x07;
        }
        else {
          date_time[3]++;
        }
      }
      else if (key == BTN3) {
        if (date_time[3] == 0x00) {
          date_time[3] = 0x31;
        }
        else if ((date_time[3] & 0x0F) == 0x00) {
          date_time[3] -= 0x07;
        }
        else {
          date_time[3]--;
        }
      }
      break;
    case MODE_ADJ_HOUR_WAIT:
      if (key == BTN1) {
        mode = MODE_ADJ_MIN_WAIT;
        break;
      }
      else if ((key == BTN2) || (key == BTN3)) {
        mode = MODE_ADJ_HOUR;  // 時刻調整はbreakせずにこのままMODE_ADJ_HOURに行って行う。
      }
      else {
        break;
      }
    case MODE_ADJ_HOUR:
      if (key == BTN1) {
        mode = MODE_ADJ_MIN;
      }
      else if (key == BTN2) {
        if (date_time[2] == 0x23) {
          date_time[2] = 0x00;
        }
        else if ((date_time[2] & 0x0F) == 0x09) {
          date_time[2] += 0x07;
        }
        else {
          date_time[2]++;
        }
      }
      else if (key == BTN3) {
        if (date_time[2] == 0x00) {
          date_time[2] = 0x23;
        }
        else if ((date_time[2] & 0x0F) == 0x00) {
          date_time[2] -= 0x07;
        }
        else {
          date_time[2]--;
        }
      }
      break;
    case MODE_ADJ_MIN_WAIT:
      if (key == BTN1) {
        mode = MODE_CLOCK;
        break;
      }
      else if ((key == BTN2) || (key == BTN3)) {
        mode = MODE_ADJ_MIN;  // 時刻調整はbreakせずにこのままMODE_ADJ_MINに行って行う。
      }
      else {
        break;
      }
    case MODE_ADJ_MIN:
      if (key == BTN1) {
        mode = MODE_CLOCK;
        count = 0;          // タイマインタラプトのカウンタをこーやってクリアするのは行儀悪い。
        date_time[0] = 0;
        Rtc.sync(date_time, 7);
      }
      else if (key == BTN2) {
        if (date_time[1] == 0x59) {
          date_time[1] = 0x00;
        }
        else if ((date_time[1] & 0x0F) == 0x09) {
          date_time[1] += 0x07;
        }
        else {
          date_time[1]++;
        }
      }
      else if (key == BTN3) {
        if (date_time[1] == 0x00) {
          date_time[1] = 0x59;
        }
        else if ((date_time[1] & 0x0F) == 0x00) {
          date_time[1] -= 0x07;
        }
        else {
          date_time[1]--;
        }
      }
      break;
    default:
      mode = MODE_CLOCK;
  }

  return;
}

void itm_man(void) {
  unsigned char value;
  unsigned char key;

  if (itm_firstf <= 1000) { // 最初の数秒はキースキャン行わない。
    itm_firstf++;
    return;
  }

  key = itm_ana_tmread();         // アナログキー読み込み
  key_now = key;                  // テスト表示用に格納
  if (mode != MODE_RTC_TEST) {
    value = itm_ana(key);         // キー入力処理
  }

  return;
}

unsigned char key_count;
unsigned char key_last;
#define ITM_SQ_SW123   0
#define ITM_SQ_SW12    1
#define ITM_SQ_SW13    2
#define ITM_SQ_SW1     3
#define ITM_SQ_SW23    4
#define ITM_SQ_SW2     5
#define ITM_SQ_SW3     6
#define ITM_SQ_SW_NON  7

void itm_ana_ini(void)
{
  unsigned long vcc;
  unsigned long tmp1;
  unsigned int count;

  vcc = analogRead(ITM_ANACH);
  vcc = (50000 * vcc) / 33792;
  vcc = 500000 / vcc;

  for (count = 0; count <= 8; count++) {
    tmp1 = ((102400 / vcc) * itm_base_boundary[count]) / 100;
    itm_boundary[count] = (int)tmp1;
  }

  key_count = 0;
  key_last = ITM_SW_NON;

  return;
}

unsigned char itm_ana_tmread(void)
{
  unsigned int val;
  unsigned int key;
  //  unsigned int ret;
  int count;

  unsigned char key_tmp;

  key = ITM_SW_NON;    // まずは入力なしとする。

  val = analogRead(ITM_ANACH);
  for (count = 0; count < 8; count++) {
    if ( (itm_boundary[count] <= val) && (val < itm_boundary[count + 1])) {
      key_tmp = count;
      break;
    }
  }

  if (key_tmp == key_last) {
    if (key_count >= 3) {
      key = key_tmp;
    }
    else {
      key_count++;
    }
  }
  else {
    key_count = 0;
  }
  key_last = key_tmp;

  return (key);
}

unsigned char itm_ana(unsigned char key)
{
  if ((key == ITM_SW1) && (BTN1_chkw == 0)) {
    adj(BTN1);
    BTN1_chkw = 1;
  }
  else if ((key == ITM_SW_NON) && (BTN1_chkw == 1)) {
    BTN1_chkw = 0;
  }

  if ((key == ITM_SW2) && (BTN2_chkw == 0)) {
    adj(BTN2);
    BTN2_chkw = 1;
  }
  else if ((key == ITM_SW_NON) && (BTN2_chkw == 1)) {
    BTN2_chkw = 0;
  }

  if ((key == ITM_SW3) && (BTN3_chkw == 0)) {
    adj(BTN3);
    BTN3_chkw = 1;
  }
  else if ((key == ITM_SW_NON) && (BTN3_chkw == 1)) {
    BTN3_chkw = 0;
  }

  return (key);
}

void disp_vfd_leo(void)
{
  unsigned char portb, portc, portd, porte, portf;
  unsigned char output_data;
  static unsigned char disp_count;
  static unsigned char count;
  static unsigned char ketaw;
  unsigned char dispketaw;

  count++;
  if (count % /*(char)*/DISP_PRE == 0) {
    disp_count++;

    // 表示データ作成
    portb = PORTB & 0x0F;
    portc = PORTC & 0x3F;
    portd = PORTC & 0x13;
    porte = PORTE & 0xBF;
    portf = PORTF & 0x8F;

    // 表示クリア
    PORTB = portb;
    PORTC = portc;
    PORTD = portd;
    PORTE = porte;
    PORTF = portf;

    // 点灯する桁更新
    if (ketaw >= (DISP_KETAMAX - 1)) {
      ketaw = 0;
    }
    else {
      ketaw++;
    }
    dispketaw = (6 - keta[ketaw]);
    // 各桁表示データ作成
    output_data = font[time[dispketaw]] | piriod[dispketaw];
    if (time[dispketaw] == 4) {
      portf |= 0x40;                        // "-"追加
    }

    // 桁
    if (dispketaw == 5) {
      portc |= 0x80;
    }
    else if (dispketaw == 4) {
      portd |= 0x40;
    }
    else if (dispketaw <= 3) {
      portb |= (0x10 << (dispketaw));
    }

    // : 表示
    disp_colon_leonardo(count, &portf);

    portd |= ((output_data & 0x01) << 2);  // dp
    portd |= ((output_data & 0x02) << 2);  // d
    portf |= ((output_data & 0x04) << 2);  // f
    portf |= ((output_data & 0x08) << 2);  // g
    portd |= ((output_data & 0x10));       // a
    portc |= ((output_data & 0x20) << 1);  // b
    portd |= ((output_data & 0x40) << 1);  // e
    porte |= ((output_data & 0x80) >> 1);  // c

    PORTB = portb;
    PORTC = portc;
    PORTD = portd;
    PORTE = porte;
    PORTF = portf;
  }
  else {
    // : 表示
    portf = PORTF;
    disp_colon_leonardo(count, &portf);
    PORTF = portf;
  }

  return;
}

void disp_colon(unsigned char disp_count, unsigned char *portc, unsigned char *portd)
{
  if (((count % COLON_PWM) >= colon_brightw)) {
    *portd |= 0x08;  // : 消灯
  }
  else {
    *portd &= 0xF7;  // : 点灯
  }

  return;
}


void disp_colon_leonardo(unsigned char disp_count, unsigned char *portf)
{
  // : 表示
  if (((disp_count % COLON_PWM)) >= colon_brightw) {
    *portf |= 0x02;                        // 消灯
  }
  else {
    *portf &= 0xFD;                        // 点灯
  }

  return;
}


void int_count(void) {
  count++;
  if (last_second != date_time[0]) {
    count = 0;
    last_second = date_time[0];
    sirial_out();                        // RTCテスト用シリアル出力
  }

  if (mode != MODE_RTC_TEST) {          // RTCテスト時はシリアル出力行うため、VFD表示しない。
    disp_vfd_leo();
  }

  return;
}
