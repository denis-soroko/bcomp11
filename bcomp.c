/*
	BCOMP11 firmware, old version.
	Use it for debug experimental functions.
	
	igorkov / fsp@igorkov.org / 2016-2017
	
	Site: igorkov.org/bcomp11
 */
#include <stdio.h>
#include <lpc11xx.h>
#include <math.h>

#include "leds.h"
#include "event.h"
#include "can.h"
#include "uart0.h"
#include <stdint.h>
#include <string.h>

#include "dbg.h"
#include "buttons.h"
#include "beep.h"
#include "obd_pids.h"
#include "obd.h"
#include "bcomp.h"

#include "adc.h"
#include "i2c.h"
#include "eeprom.h"
#include "config.h"
#include "oled128.h"
#include "graph.h"
#include "wheels.h"

#include "errors.h"

bcomp_t bcomp;

/*
	bcomp_proc()
	���������� �� OBD.C
	������ PID-s ������, ������� ��������� � ��������� bcomp.
 */
void bcomp_proc(int pid, uint8_t *data, uint8_t size) {
	data = data-1;
	/*  Details from: http://en.wikipedia.org/wiki/OBD-II_PIDs */
	switch (pid) {
	//   A       B       C       D       E
	// data[3] data[4] data[5] data[6] data[7]
	case ENGINE_COOLANT_TEMP:
		// A-40 [�C]
		bcomp.t_engine = data[3] - 40;
		DBG("Engine temperature = %d�C\r\n", (int)bcomp.t_engine);
		break;
	case ENGINE_RPM:
		// ((A*256)+B)/4 [RPM]
		bcomp.rpms = bcomp.rpm = (uint32_t)((data[3]*256) + data[4])/4;
		DBG("Engine RPM = %drpm\r\n", (int)bcomp.rpm);
		break;
	case VEHICLE_SPEED:
		// A [km]
		bcomp.speed = data[3];
		DBG("Speed = %dkm\r\n", (int)bcomp.speed);
		break;
	case ECU_VOLTAGE:
		// ((A*256)+B)/1000 [V]
		bcomp.v_ecu = (float)((data[3]*256)+data[4])/1000.0f;
		DBG("Volt = %d.%dV\r\n", (int)bcomp.v_ecu, (int)(bcomp.v_ecu*10.0f)%10);
		break;
	case PAJERO_AT_INFO:
		// F-40 [�C]
		bcomp.t_akpp = (data[8]-40);
		DBG("AT temperature = %d�C\r\n", (int)bcomp.t_akpp);
		break;
	case GET_VIN:
		// VIN-��� �������, �� ��������� ������ ��� ������!
		obd_act_set(GET_VIN, 0);
		// ��������� VIN:
		//strcpy(bcomp.vin, (char*)&data[4]);
		memcpy(bcomp.vin, (char*)&data[4], 19);
		bcomp.vin[19] = 0;
		DBG("VIN = %s\r\n", bcomp.vin);
		break;
	case STATUS_DTC:
		if (data[3] & 0x80) {
			// MIL Light on
			//bcomp.mil = 1;
			// FIX: ���� �������� ������ ����� ������ ���� ������
			obd_act_set(FREEZE_DTC, 1);
			DBG("MIL ON!!!\r\n");
		} else {
			// MIL Light off
			bcomp.mil = 0;
			DBG("MIL OFF!!!\r\n");
		}
		break;
	case FREEZE_DTC: {
#if defined( _DBGOUT )
		char error_code[8];
#endif
		// ������������ ������ ����:
		obd_act_set(FREEZE_DTC, 0);
		// ��������� ���:
		bcomp.e_code = data[3] * 256 + data[4];
		bcomp.mil = 1;
#if defined( _DBGOUT )
		error_decrypt(bcomp.e_code, error_code);
		DBG("Trouble code detect: %s\r\n", error_code);
#endif
		break;
	}
	case PAJERO_ODO_INFO:
		// ������������ ������ ��������:
		obd_act_set(PAJERO_ODO_INFO, 0);
		// ��������� ����������� ��������:
		bcomp.odometer = (data[4] * 256 + data[5]) * 256 + data[6];
		DBG("Odometer in ECU: %d", bcomp.odometer);
		break;
	default:
		break;
	}
}

/*
	bcomp_raw()

	���������� �� OBD.C.

	��������� ����� ������ ����. 
	��������� ���������� �� ������� ������, �������� �� ����.
	������ ��������� ���������� ��� Pajero Sport 2nd generation.
 */
void bcomp_raw(int pid, uint8_t *data, uint8_t size) {
	switch (pid) {
	case 0x0215:
		bcomp.speed = ((uint32_t)data[0] * 256 + data[1]) / 128;
		break;
	case 0x0218:
		switch (data[2]) {
		case 0x11:
		case 0x22:
		case 0x33:
		case 0x44:
		case 0x55:
		case 0xdd:
		case 0xbb:
			bcomp.at_present = 1; // AT-������� � �������!
			// ���������� ��������:
			bcomp.at_drive = (uint8_t)data[2] & 0x0F;
			break;
		}
		break;
	case 0x0236:
	case 236:
		// ������� � ������� ��������� ����, 
		// ���� ���������� ��� ����� ������:
		memcpy(bcomp.esc_data, data, 8);
		bcomp.esc_id = pid;
		break;
	case 0x0308:
		bcomp.rpms = bcomp.rpm = (uint32_t)data[1] * 256 + data[2];
		break;
	case 0x0608:
		bcomp.t_engine = (int32_t)data[0] - 40;
		// �������� ������� ����������� �������
		bcomp.raw_fuel = (int32_t)data[5]*256 + data[6];
		break;
	}	
}

/*
	bcomp_calc()
	������� �������� ������. ���������� ������ �������.
	�������� �������������� ��������� � �������� ��������� ���������� ��������.
 */
void bcomp_calc(void) {
	int i;
	double d_dist = (double)bcomp.speed / 3600.0f * bconfig.speed_coeff * 1000.0f;
	double d_fuel = (double)bcomp.raw_fuel / 3600.0f * bconfig.fuel_coeff / 1000.0f;

	// ��������� ��������� �������:
	bcomp.fuel += d_fuel;
	bcomp.dist += d_dist;

	// ���������� ���������:
	if (bcomp.rpm) {
		for (i=0; i<2; i++) {
			bcomp.trip[i].dist += d_dist;
			bcomp.trip[i].fuel += d_fuel;
			bcomp.trip[i].time++;
		}
	}

	// ��������� ����� (������ ���� ��������� �������):
	if (bcomp.rpm) {
		// ������� ������:
		bcomp.time++;
		// �������� ���������:
		bcomp.moto_time++;
		bcomp.moto_time_service++;
		// ���������:
		bcomp.moto_dist += d_dist;
		bcomp.moto_dist_service += d_dist;
	}

#if 1
	if ((bcomp.time%30) == 0) {
		// ������� ��� �������� ����������� �������:
		bcomp.log[(bcomp.time/30)%20].fuel = bcomp.fuel;
		bcomp.log[(bcomp.time/30)%20].dist = bcomp.dist;
	}
#endif

	// �������� ���������:
	bcomp.rpm = 0;

	event_set(bcomp_calc, 1000);
}

volatile uint8_t save_flag = 0;
void save_params(void) {
	// ��������� ������� � ��������:
	config_save(CPAR_MOTO_GLOB, (uint8_t*)&bcomp.moto_time, CPAR_MOTO_GLOB_SIZE);
	config_save(CPAR_MOTO_SERV, (uint8_t*)&bcomp.moto_time_service, CPAR_MOTO_SERV_SIZE);
	config_save(CPAR_DIST_GLOB, (uint8_t*)&bcomp.moto_dist, CPAR_DIST_GLOB_SIZE);
	config_save(CPAR_DIST_SERV, (uint8_t*)&bcomp.moto_dist_service, CPAR_DIST_SERV_SIZE);
	// ������������� ���������� �������� �:
	config_save(CPAR_TRIPA_DIST, (uint8_t*)&bcomp.trip[0].dist, CPAR_TRIPA_DIST_SIZE);
	config_save(CPAR_TRIPA_TIME, (uint8_t*)&bcomp.trip[0].time, CPAR_TRIPA_TIME_SIZE);
	config_save(CPAR_TRIPA_FUEL, (uint8_t*)&bcomp.trip[0].fuel, CPAR_TRIPA_FUEL_SIZE);
	// ������������� ���������� �������� �:
	config_save(CPAR_TRIPB_DIST, (uint8_t*)&bcomp.trip[1].dist, CPAR_TRIPB_DIST_SIZE);
	config_save(CPAR_TRIPB_TIME, (uint8_t*)&bcomp.trip[1].time, CPAR_TRIPB_TIME_SIZE);
	config_save(CPAR_TRIPB_FUEL, (uint8_t*)&bcomp.trip[1].fuel, CPAR_TRIPB_FUEL_SIZE);
}

void save_settings(void) {
	// only in verison V2.
}

void bcomp_save(void) {
	save_flag |= 1;
	event_set(bcomp_save, 30000);
}

extern int melody1[];
extern int melody_warning[];
extern int melody_wrep[];
extern int melody_start[];

void ProtectDelay(void) {
	int n;
	for (n = 0; n < 100000; n++) { __NOP(); } 
}

volatile uint8_t val_Tx = 0, val_Rx = 0;

void set_warning(int num) {
	if (bcomp.page == 0) {
		graph_ico16(108,num*8,ico16_warning_data,16);
	}
	if (beep_is_play()) {
		return;
	}
	beep_play(melody_wrep);
}

int main(void) {
	int i = 0; // ���������� ��������.
	char str[32];
	int ms;
	int state;
	uint8_t reset_counter = 0;

	event_init();
	leds_init();
#if defined( _DBGOUT )
	uart0_init(115200);
#endif
	i2c_init();
	beep_init();
	button_init();
	adc_init();

	led_red(1);
	delay_ms(100);
	led_green(1);
	led_red(0);
	delay_ms(1000); // �������� ����� ������� �������� ������.
	led_green(0);

	if (bconfig.ee_size) {
		// ���������� ����� ��� �����������:
		if (config_read(CPAR_PAGE, (uint8_t*)&bcomp.page, CPAR_PAGE_SIZE)) {
			bcomp.page = 0;
		}
		DBG("page = %d\r\n", bcomp.page);
	
		// ��������� ������� � ��������:
		if (config_read(CPAR_MOTO_GLOB, (uint8_t*)&bcomp.moto_time, CPAR_MOTO_GLOB_SIZE)) {
			bcomp.moto_time = 0;
		}
		if (config_read(CPAR_MOTO_SERV, (uint8_t*)&bcomp.moto_time_service, CPAR_MOTO_SERV_SIZE)) {
			bcomp.moto_time_service = 0;
		}
		// ���������� ������� � ��������:
		if (config_read(CPAR_DIST_GLOB, (uint8_t*)&bcomp.moto_dist, CPAR_DIST_GLOB_SIZE)) {
			bcomp.moto_dist = 0.0f;
		}
		if (config_read(CPAR_DIST_SERV, (uint8_t*)&bcomp.moto_dist_service, CPAR_DIST_SERV_SIZE)) {
			bcomp.moto_dist_service = 0.0f;
		}
		
		// ������������� ���������� �������� �:
		if (config_read(CPAR_TRIPA_DIST, (uint8_t*)&bcomp.trip[0].dist, CPAR_TRIPA_DIST_SIZE)) {
			bcomp.trip[0].dist = 0.0f;
		}
		if (config_read(CPAR_TRIPA_TIME, (uint8_t*)&bcomp.trip[0].time, CPAR_TRIPA_TIME_SIZE)) {
			bcomp.trip[0].time = 0;
		}
		if (config_read(CPAR_TRIPA_FUEL, (uint8_t*)&bcomp.trip[0].fuel, CPAR_TRIPA_FUEL_SIZE)) {
			bcomp.trip[0].fuel = 0.0f;
		}

		// ������������� ���������� �������� �:
		if (config_read(CPAR_TRIPB_DIST, (uint8_t*)&bcomp.trip[1].dist, CPAR_TRIPB_DIST_SIZE)) {
			bcomp.trip[1].dist = 0.0f;
		}
		if (config_read(CPAR_TRIPB_TIME, (uint8_t*)&bcomp.trip[1].time, CPAR_TRIPB_TIME_SIZE)) {
			bcomp.trip[1].time = 0;
		}
		if (config_read(CPAR_TRIPB_FUEL, (uint8_t*)&bcomp.trip[1].fuel, CPAR_TRIPB_FUEL_SIZE)) {
			bcomp.trip[1].fuel = 0.0f;
		}
		
		//todo
		//������ ����������
	}

	// ������������� ����������:
	bcomp.time = 0;
	bcomp.t_engine = 0xFFFF;
	bcomp.rpm = 0;
	bcomp.speed = 0;
	bcomp.at_drive = 0xFF;
	bcomp.v_ecu = NAN;
	bcomp.t_akpp = 0xFFFF;
	bcomp.at_present = 0;
	bcomp.dist = 0.0f;
	bcomp.fuel = 0.0f;
	bcomp.vin[0] = 0;
	bcomp.odometer = -1;
	bcomp.angle = 0;

	// �������� "����������" ���. ������ ��� ������������ ��� �������� ������� �������.
	for (i=0; i<sizeof(bcomp.log)/sizeof(pars_t); i++) {
		bcomp.log[i].fuel = 0.0f;
		bcomp.log[i].dist = 0.0f;
	}

	// �������������� ����������� �������:
	event_set(bcomp_calc, 1000); delay_ms(10);
	if (bconfig.ee_size) {
		// ������, ���� ���� EEEPROM, ����������� ���������� ����������:
		event_set(bcomp_save, 30000); delay_ms(10);
	}

	// �������������� �����:
	oled_init(bconfig.contrast, 0);
	graph_clear();

	// ��������� ����� (��������):
#if !defined( __GCC__ )
	//graph_ico64(0,0,ico64_mitsu_data,65);
	graph_pic(&ico64_mitsu, 0,0);
#endif
	_sprintf(str, "BCOMP");
	graph_puts16(64,1*8,0,str); 
	_sprintf(str, "igorkov.org");
	graph_puts8(58,3*8,0,str); 
	_sprintf(str, "    2016   ");
	graph_puts8(58,4*8,0,str);
	if (bconfig.ee_size) {
		_sprintf(str, "  EE: %2dkB", bconfig.ee_size);
	} else {
		_sprintf(str, "  EE: noset");
	}
	graph_puts8(58,5*8,0,str); 
	graph_update();
	if (bconfig.start_sound) {
		beep_play(melody_start);
	}
	
	// �������� N ������, �� ������� - ���������.
	ms = get_ms_timer();
	while ((get_ms_timer() - ms) < bconfig.start_delay*1000) {
		__WFI();
		state = button_read();
		if (state) {
			break;
		}
	}

	// ��������� OBD-�������� ������ ����� ��������:
	obd_init();

	// �������� ���� ������:
	while (1) {
		// ===========================================
		// ��������� ������:
		// ===========================================

		// ��������� ������:
		ms = get_ms_timer();
		while ((get_ms_timer() - ms) < 400) {
			__WFI();
			state = button_read();
			if (state & BUTT_SW1) {
				reset_counter = 0;
				if (bconfig.ee_size) {
					bcomp.page = (bcomp.page + 1) % 10;
				} else {
					// ���� ��� EEPROM, ������ 1�� � ��������� ������ ���������:
					bcomp.page = (bcomp.page == 0) ? 4 : 0;
				}
				if (bconfig.ee_size) {
					config_save(CPAR_PAGE, (uint8_t*)&bcomp.page, CPAR_PAGE_SIZE);
				}
				break;
			}
			if (state & BUTT_SW2) {
				switch (bcomp.page) {
				case 0:
					__WFI();
					break;
				case 1:
					__WFI();
					// ������������� ���������� �������� �:
					bcomp.trip[0].dist = 0.0f;
					bcomp.trip[0].fuel = 0.0f;
					bcomp.trip[0].time = 0;
					if (bconfig.ee_size) {
						config_save(CPAR_TRIPA_DIST, (uint8_t*)&bcomp.trip[0].dist, CPAR_TRIPA_DIST_SIZE);
						config_save(CPAR_TRIPA_TIME, (uint8_t*)&bcomp.trip[0].time, CPAR_TRIPA_TIME_SIZE);
						config_save(CPAR_TRIPA_FUEL, (uint8_t*)&bcomp.trip[0].fuel, CPAR_TRIPA_FUEL_SIZE);
					}
					break;
				case 2:
					__WFI();
					// ������������� ���������� �������� �:
					bcomp.trip[1].dist = 0.0f;
					bcomp.trip[1].fuel = 0.0f;
					bcomp.trip[1].time = 0;
					if (bconfig.ee_size) {
						config_save(CPAR_TRIPB_DIST, (uint8_t*)&bcomp.trip[1].dist, CPAR_TRIPB_DIST_SIZE);
						config_save(CPAR_TRIPB_TIME, (uint8_t*)&bcomp.trip[1].time, CPAR_TRIPB_TIME_SIZE);
						config_save(CPAR_TRIPB_FUEL, (uint8_t*)&bcomp.trip[1].fuel, CPAR_TRIPB_FUEL_SIZE);
					}
					break;
				case 3:
					__WFI();
					reset_counter++;
					if (reset_counter >= 10) {
						// ������� EEPROM:
						ee_clear();
						// ������ �������:
						beep_play((int*)melody_warning);
						// ���������������!
						while (1);
					} else if (reset_counter == 3) {
						// ���������� ������ ����� 3� �������!
						// ������������� ���������� ��������:
						bcomp.moto_time_service = 0;
						bcomp.moto_dist_service = 0.0f;
						if (bconfig.ee_size) {
							config_save(CPAR_MOTO_SERV, (uint8_t*)&bcomp.moto_time_service, CPAR_MOTO_SERV_SIZE);
							config_save(CPAR_DIST_SERV, (uint8_t*)&bcomp.moto_dist_service, CPAR_DIST_SERV_SIZE);
						}
					}
					break;
				case 4:
					__WFI();
					break;
				case 8:
					bcomp.angle++;
					if (bcomp.angle == 25) {
						bcomp.angle = -24;
					}
					break;
				}
				break;
			}
		}

		// ===========================================
		// �������� ������:
		// ===========================================

		// ������� �������� �����:
		graph_clear();
		switch (bcomp.page) {
		case 0: // main
			// ������ 1:
			if (bcomp.t_engine == 0xFFFF) {
				_sprintf(str, "---�C");
			} else {
				_sprintf(str, "%3d�C", bcomp.t_engine);
			}
			graph_ico16(24,0*8,ico16_engine_data,16); 
			graph_puts16(48,0*8,0,str);
			// ������ 2
			if (bcomp.at_present == 0) {
				graph_ico16(24,2*8,ico16_mt_data,16);
			} else {
				if (bcomp.t_akpp == 0xFFFF) {
					_sprintf(str, "---�C");
				} else {
					_sprintf(str, "%3d�C", bcomp.t_akpp);
				}
				graph_ico16(24,2*8,ico16_at_data,16); 
				graph_puts16(48,2*8,0,str);
			}
			// ������ 3
			// NOTE: ������ ��� ���������� ������ ����� ��� ����� 10 �����, ��� �������� 
			// ��������, ��-�� �������� �������� � ������ "+1". ������, ��� ������ ���������� 
			// �������, ���������� ������ �������� � �������� �� 10 �����.
			if ((bcomp.log[(bcomp.time/30)%20].dist - bcomp.log[(bcomp.time/30+1)%20].dist) > 1000.0f) {
				// ���� �� 10 ����� �������� ������ 1��:
				float d_fuel = (bcomp.log[(bcomp.time/30)%20].fuel - bcomp.log[(bcomp.time/30+1)%20].fuel);
				float d_dist = (bcomp.log[(bcomp.time/30)%20].dist - bcomp.log[(bcomp.time/30+1)%20].dist)/1000.0f;
				float fuel_km = d_fuel / d_dist * 100.0f;
				if (fuel_km < 50.0f) {
					_sprintf(str, "%2d.%d", (int)fuel_km, (int)(fuel_km*10)%10);
				} else {			  
					_sprintf(str, "--.-");
				}
			} else {			  
				_sprintf(str, "--.-");
			}
			graph_ico16(24,4*8,ico16_fuel_data,16);
			graph_puts16(48,4*8,0,str);
			graph_ico16(96,4*8,ico16_100kms_data,22); 
			// RPM (old):	
			//if (bcomp.moto_rpms == 0xFFFF) {
			//	_sprintf(str, "%5d", 0);
			//} else {
			//	_sprintf(str, "%5d", bcomp.moto_rpms);
			//}
			//graph_ico16(24,4*8,ico16_rpm_data,16); 
			//graph_puts16(48,4*8,0,str);
			// ������ 4
			if (isnan(bcomp.v_ecu))	{
				_sprintf(str, "--.-V");
			} else {
				_sprintf(str, "%2d.%dV", (int)(bcomp.v_ecu), (int)(bcomp.v_ecu*10)%10);
			}
			graph_ico16(24,6*8,ico16_battery_data,16); 
			graph_puts16(48,6*8,0,str);
			break;
		case 1: // trip a
		case 2: // trip b
			// ����� ������ ����: 
			if (bcomp.page == 1) {
				graph_puts16(0,0*8,0,"A");
			} else {
				graph_puts16(0,0*8,0,"B");
			}
			// ������ 1:
			_sprintf(str, "%4d", (int)bcomp.trip[bcomp.page-1].dist/1000);
			graph_ico16(24,0*8,ico16_road_data,16); 
			graph_puts16(48,0*8,0,str);
			graph_ico16(96,0*8,ico16_km_data,19); 
			// ������ 2:
			_sprintf(str, "%4d�", (int)bcomp.trip[bcomp.page-1].fuel);
			graph_ico16(24,2*8,ico16_fuel_data,16);
			graph_puts16(48,2*8,0,str);
			// ������ 3:
			// 5 ������ - ������� ������, 5 ������ - ������� ��������
			if ((bcomp.time % 10) > 4) {
				// ������ 5 ����� �������, ������� ������� ��������:
				if (bcomp.trip[bcomp.page-1].time > 300) {
					float speed = 3.6f * bcomp.trip[bcomp.page-1].dist / bcomp.trip[bcomp.page-1].time;
					_sprintf(str, " %3d", (int)speed);
				} else {
					_sprintf(str, " ---");
				}
				graph_ico16(96,4*8,ico16_kmh_data,20); 
			} else {
				if (bcomp.trip[bcomp.page-1].dist > 5000.0f) {
					float fuel = (bcomp.trip[bcomp.page-1].fuel/(bcomp.trip[bcomp.page-1].dist/1000.0f))*100.0f;
					_sprintf(str, "%2d.%d", (int)fuel, (int)(fuel*10.0f)%10);
				} else {
					_sprintf(str, "--.-");
				}
				graph_ico16(96,4*8,ico16_100kms_data,22); 
			}
			graph_puts16(48,4*8,0,str);
			// ������ 4:
			_sprintf(str, "%2d�%02d�", bcomp.trip[bcomp.page-1].time/3600, (bcomp.trip[bcomp.page-1].time/60)%60);
			graph_ico16(24,6*8,ico16_time_data,16);
			graph_puts16(48,6*8,0,str);
			break;
		case 3: // service
			// ������ 1:
				_sprintf(str, "%5d�", bcomp.moto_time/3600 + bconfig.moto_time_offset);
			graph_ico16(24,0*8,ico16_time_data,16); 
			graph_puts16(48,0*8,0,str);
			// ������ 2:
			_sprintf(str, "%5d", (int)bcomp.moto_dist/1000 + bconfig.moto_dist_offset);
			graph_ico16(24,2*8,ico16_road_data,16); 
			graph_puts16(48,2*8,0,str);
			graph_ico16(108,2*8,ico16_km_data,19); 
			// ������ 3:
			_sprintf(str, "%5d�", bcomp.moto_time_service/3600);
			graph_ico16(24,4*8,ico16_time_data,16); 
			graph_puts16(48,4*8,0,str);
			// ������ 4:
			_sprintf(str, "%5d", (int)bcomp.moto_dist_service/1000);
			graph_ico16(24,6*8,ico16_road_data,16); 
			graph_puts16(48,6*8,0,str);
			graph_ico16(108,6*8,ico16_km_data,19);
			break;
		case 4:
			// ������ 1:
			_sprintf(str, "%4d", (int)((float)bcomp.speed*bconfig.speed_coeff));
			graph_ico16(24,0*8,ico16_road_data,16); 
			graph_puts16(48,0*8,0,str);
			graph_ico16(48+48,0*8,ico16_kmh_data,20); 
			// ������ 2:
			_sprintf(str, "%4d", bcomp.rpms);
			graph_ico16(24,2*8,ico16_rpm_data,16); 
			graph_puts16(48,2*8,0,str);
			// ������ 3:
			if (bcomp.at_present == 0) {
				graph_ico16(24,4*8,ico16_mt_data,16); 
			} else {
				graph_ico16(24,4*8,ico16_at_data,16); 
				if (bcomp.at_drive == 0x00) {
					_sprintf(str, "N");
				} else
				if (bcomp.at_drive == 0x01) {
					_sprintf(str, "D/1");
				} else
				if (bcomp.at_drive == 0x02) {
					_sprintf(str, "D/2");
				} else
				if (bcomp.at_drive == 0x03) {
					_sprintf(str, "D/3");
				} else
				if (bcomp.at_drive == 0x04) {
					_sprintf(str, "D/4");
				} else
				if (bcomp.at_drive == 0x05) {
					_sprintf(str, "D/5");
				} else
				if (bcomp.at_drive == 0x0d) {
					_sprintf(str, "P");
				} else
				if (bcomp.at_drive == 0x0b) {
					_sprintf(str, "R");
				}
				graph_puts16(48,4*8,0,str);
			}
			// ������ 4:
			_sprintf(str, "%d�", (int)bcomp.dist);
			graph_ico16(24,6*8,ico16_road_data,16); 
			graph_puts16(48,6*8,0,str);
			break;
		case 5:
			// �������� ����� ������ ESC (������ ��������� �������� ������):
			graph_puts16(0,0*8,0,"WHEELS");
			_sprintf(str, "ID=%d, DATA:", bcomp.esc_id);
			graph_puts8(0,3*8,0,str);
			_sprintf(str, "%02x %02x %02x %02x",
				bcomp.esc_data[0], bcomp.esc_data[1], bcomp.esc_data[2], bcomp.esc_data[3]);
			graph_puts8(0,4*8,0,str);
			_sprintf(str, "%02x %02x %02x %02x",
				bcomp.esc_data[4], bcomp.esc_data[5], bcomp.esc_data[6], bcomp.esc_data[7]);
			graph_puts8(0,5*8,0,str);
			break;
		case 6:
			// ��������� �������� ��������:
			graph_puts16(0,0*8,0,"ODOMETER");
			if (bcomp.odometer == -1) {
				obd_act_set(PAJERO_ODO_INFO, 1);
			}
			_sprintf(str, "%d��", bcomp.odometer);
			graph_puts16(0,2*8,0,str);
			break;
		case 7:
			// ��������� VIN-���:
			if (bcomp.vin[0] == 0) {
				obd_act_set(GET_VIN, 1);
			}
			graph_puts16(0,0*8,0,"VIN");
			memcpy(str, bcomp.vin, 10); str[10] = 0;
			graph_puts16(0,16,0,str);
			memcpy(str, &bcomp.vin[10], 10); str[10] = 0;
			graph_puts16(0,32,0,str);
			break;
		case 8:
			// ���� �����:
			graph_puts16(64,0,1,"WHEELS");
#if !defined( __GCC__ )
			graph_line(40+4,40,88-4,40);
			draw_rect(40,40,bcomp.angle);
			draw_rect(88,40,bcomp.angle);
#else
			graph_puts16(64,16,1,"UNSUPPORT");
#endif
			break;
		case 9:	{
			float value;
			// ��������� ������ � ���������� ������:
			graph_puts16(64,0,1,"ADC");
			_sprintf(str, "1:%03x", adc_get(ADC_CH1));
			graph_puts16(0,16,0,str);
			_sprintf(str, "2:%03x", adc_get(ADC_CH2));
			graph_puts16(0,32,0,str);
#if !defined( __GCC__ )
			value = analog_temp(&bconfig.termistor);
			if (value == NAN) {
				_sprintf(str, "T:NAN");
			} else {
				_sprintf(str, "T:%2d.%d�C", (int)value, (int)(value*10)%10);
			}
			graph_puts16(0,48,0,str);
#endif
			break; }
		default:
			bcomp.page = 0;
			break;
		}

		// ===========================================
		// ��������:
		// ===========================================

		// �������� ����������� ���������:
		if (bcomp.t_engine != 0xFFFF &&
			bcomp.t_engine >= bconfig.t_engine_warning) {
			set_warning(0);
		}
		// �������� ����������� �������:
		if (bcomp.t_akpp != 0xFFFF &&
			bcomp.t_akpp >= bconfig.t_akpp_warning) {
			set_warning(2);
		}
		// �������� ���������� �������� ����:
		if (isnan(bcomp.v_ecu) == 0 &&
			(bcomp.v_ecu < bconfig.v_min ||
			bcomp.v_ecu > bconfig.v_max)) {
			set_warning(6);
		}
		// ������� ����� ������:
		if (bcomp.mil) {
			//
		}

		// ===========================================
		// ���������� ������:
		// ===========================================
		ms = get_ms_timer(); 
		//i2c_busy = 1;
		graph_update();
		//i2c_busy = 0;
		ms = get_ms_timer() - ms;
		DBG("graph_update() work %dms\r\n", ms);

		// ���������� ���������� ����������:
		if (save_flag & 0x01) {
			save_params();
			save_flag &= ~0x01;
		} else
		if (save_flag & 0x02) {
			save_settings();
			save_flag &= ~0x02;
		}
	}
}
