/*
	elog.c

	������ ��� ����������� �������� �� ������� ����� �������� ���������� 
	�� �������� ��������� ����������. ������� ��� ��������� ������������ 
	������ �������. ��� ������ ���� ��������� ������� UART-����������.

	���������� ��������� ����� ������ �� �����:
	time;speed;rpm;trans;batt;dfuel;t_eng;t_akpp;t_ext;dist;gdist;mil;\r\n
	1234567891;56;1560;D3;13.8;3.86;79;95;24;13847;15483337;0;\r\n

	���:
	 0 - time     - ����� ������� (����� ������ ��������� � ��������).
	 1 - speed    - �������� (��/�).
	 2 - rpm      - ������� ��������� (��/���).
	 3 - trans    - �������� ����������� (PRND12345).
	 4 - batt     - �������� ���������� (�).
	 5 - fuel     - ������� ������� � ���� (�).
	 6 - dfuel    - ��������������� �� ����� ������� (�).
	 7 - lon      - ������.
	 8 - lat      - �������.
	 9 - gtime    - ����� GPS � ������� HH-MM-SS.
	10 - gdate    - ���� GPS � ������� DD-MM-YYYY.
	11 - t_eng    - ����������� ��������� (�C).
	12 - t_akpp   - ����������� ������� ������� (�C).
	13 - t_ext    - ����������� ��������� ������� (�C).
	14 - p_fuel   - �������� � ��������� ����� (���)
	15 - p_intake - �������� �� �������� ���������� (���)
	16 - dist     - ������� ������� �� ����� (�).
	17 - gdist    - ������� ������� �� ��� ����� (�).
	18 - mil      - ����� ������ ��������� (0 - ��� ������, 1 - ���� �������� ������).
	
	igorkov / 2016-2017 / igorkov.org/bcomp11v2
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "event.h"
#include "uart0.h"
#include "bcomp.h"
#include "dbg.h"
#include "errors.h"

#include "elog.h"

#define ELOG_VIN_FLAG   0x01
#define ELOG_MIL_FLAG   0x02
#define ELOG_START_FLAG 0x04

static uint8_t elog_flags = 0;
static char elog_str[128];

// ��������� ������, ������������ ��� ���������� �������� ����� � ��������� � ����� ���������:
// COLUMN:                0    1     2   3     4    5     6     7   8   9     10    11    12     13    14     15    16   17    18  
const char elog_info[] = "time;speed;rpm;trans;batt;fuel;dfuel;lon;lat;gtime;gdate;t_eng;t_akpp;t_ext;p_fuel;p_air;dist;gdist;mil;\r\n";

void elog_proc(void) {
	int offset;
	// ������ ������ ����:
	if ((elog_flags & ELOG_START_FLAG) == 0) {
		// �������� ��������:
		elog_flags |= ELOG_START_FLAG;
		// ���������� ������ �� ����������� ��������:
		memcpy(elog_str, elog_info, strlen(elog_info));
		goto elog_send;
	}
	// ����������� �������� VIN-������:
	if ((elog_flags & ELOG_VIN_FLAG) == 0 &&
		bcomp.vin[0] != 0) {
		elog_flags |= ELOG_VIN_FLAG;
		// ���������� ������ �� ����������� �������� VIN:
		_sprintf(elog_str, "VIN;%s\r\n", bcomp.vin);
		// ��������� �� ��������:
		goto elog_send;
	}
	// ���� ���� ���� ������, ����� ������ � ���:
	if (bcomp.mil) {
		if ((elog_flags & ELOG_MIL_FLAG) == 0) {
			// ��������� ��������� ������:
			char error_code[8];
			// ������� ������:
			elog_flags |= ELOG_MIL_FLAG;
			// �������������� ������.
			error_decrypt(bcomp.e_code, error_code);
			// ���������� ������ �� ����������� �������� VIN:
			_sprintf(elog_str, "ERROR;%s\r\n", error_code);
			// ��������� �� ��������:
			goto elog_send;
		}
	} else {
		// ���� ������ ��������, ���������� � ����:
		if (bcomp.mil == 0) {
			elog_flags &= ~ELOG_MIL_FLAG;
		}
	}
	// ������������ �������:
	//  0 - time   - ����� ������� � UNIX-������� (���� �������, ����� ���������� ������� ������).
	offset = 0;
	_sprintf(&elog_str[offset], "%d;", bcomp.utime?bcomp.utime:bcomp.time);
	//  1 - speed  - �������� (��/�).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.speed);
	//  2 - rpm    - ������� ��������� (��/���).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.rpms);
	//  3 - trans  - �������� ����������� (PRND12345).
	offset = strlen(elog_str);
	switch(bcomp.at_drive) {
	case 0x00: _sprintf(&elog_str[offset], "N;");  break;
	case 0x01: _sprintf(&elog_str[offset], "D1;");  break;
	case 0x02: _sprintf(&elog_str[offset], "D2;");  break;
	case 0x03: _sprintf(&elog_str[offset], "D3;");  break;
	case 0x04: _sprintf(&elog_str[offset], "D4;");  break;
	case 0x05: _sprintf(&elog_str[offset], "D5;");  break;
	case 0x0b: _sprintf(&elog_str[offset], "R;");   break;
	case 0x0d: _sprintf(&elog_str[offset], "P;");   break;
	default:   _sprintf(&elog_str[offset], "UNK;"); break;
	}
	//  4 - batt   - �������� ���������� (�).
	offset = strlen(elog_str);
	if (isnan(bcomp.v_ecu)) {
		_sprintf(&elog_str[offset], "0.0;");
	} else {
		_sprintf(&elog_str[offset], "%d.%d;", (int)bcomp.v_ecu, (int)(bcomp.v_ecu*10)%10);
	}
	//  5 - fuel  - ��������������� �� ����� ������� (�).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d.%02d;", (int)bcomp.fuel_level, (int)(bcomp.fuel_level*100)%100);
	//  6 - dfuel  - ��������������� �� ����� ������� (�).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d.%02d;", (int)bcomp.fuel, (int)(bcomp.fuel*100)%100);
	// GPS block:
	offset = strlen(elog_str);
	if (bcomp.g_correct) {
		//  7 - lon
		_sprintf(&elog_str[offset], "%s;", bcomp.gps_val_lon);
		//	8 - lat
		offset = strlen(elog_str);
		_sprintf(&elog_str[offset], "%s;", bcomp.gps_val_lat);
		//	9 - gtime
		offset = strlen(elog_str);
		_sprintf(&elog_str[offset], "%s;", bcomp.gps_val_time);
		//  10 - gdate
		offset = strlen(elog_str);
		_sprintf(&elog_str[offset], "%s;", bcomp.gps_val_date);
	} else {
		// 7,8,9,10 - nop
		_sprintf(&elog_str[offset], ";;;;");
	}	
	// 11 - t_eng  - ����������� ��������� (�C).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.t_engine);
	// 12 - t_akpp - ����������� ������� ������� (�C).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.t_akpp);
	// 13 - t_ext  - ����������� ��������� ������� (�C).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.t_ext);
	// 14 - p_fuel
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.p_fuel);
	// 15 - p_intake
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.p_intake);
	// 16 - dist   - ������� ������� �� ����� (�).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.dist);
	// 17 - gdist  - ������� ������� �� ��� ����� (�).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.moto_dist);
	// 18 - mil    - ����� ������ ��������� (0 - ��� ������, 1 - ���� �������� ������).
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "%d;", (int)bcomp.mil);
	// string end:
	offset = strlen(elog_str);
	_sprintf(&elog_str[offset], "\r\n");
elog_send:
	// ���������� ������ �� ����������� ��������:
	uart0_puts(elog_str);
}

