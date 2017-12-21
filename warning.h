#ifndef __WARNING_H__
#define __WARNING_H__

// ������ (�� �����������, �� ������� � �������:
#define WARNING_ID_CONNECT   0
#define WARNING_ID_ENGINE    1
#define WARNING_ID_T_ENGINE  2
#define WARNING_ID_T_AT      3
#define WARNING_ID_BATTERY   4
#define WARNING_ID_T_EXT     5
#define WARNING_ID_GPS       6
#define WARNING_ID_SERVICE   7
#define WARNING_ID_FUEL      8

#define WARN_MAX_NUM    9
#define WARN_FLAG_ACT   0x01
#define WARN_FLAG_HIDE  0x02

typedef struct {
	uint8_t flags;  // ����� ���������.
	uint8_t res[3]; // 
	int time;       // ������� ��� �������� ������� �������.
} ecu_error_t, *pecu_error_t;

extern void bcomp_warning(void);
extern int warning_show(int *act);
extern void warning_check(void);
void warning_init(void);

#endif
