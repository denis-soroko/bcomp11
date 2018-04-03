/*
	buttons.c
	
	���������� buttons.c. ���� 2 ������ ���������� ������:
	1. ��������� �������� �������, ��� ���������� ������� ���� �������.
	2. �������� � ������� �������, ��� ��������� �������.
	
	igorkov / 2017 / igorkov.org/bcomp11v2
 */
#if !defined( WIN32 )
#include <LPC11xx.h>
#endif
#include <string.h>
#include "event.h"
#include "buttons.h"

static int state;
static int last_state[MAX_BUTT];

#if defined( WIN32 )
extern int iMButton;
#define SW1_READ ((iMButton&(1<<0))?1:0)
#define SW2_READ ((iMButton&(1<<1))?1:0)
#else
#define SW1_READ ((LPC_GPIO0->DATA & (1UL<<1))?0:1)
#define SW2_READ ((LPC_GPIO0->DATA & (1UL<<3))?0:1)
#endif

int button_state(int n) {
	switch (n) {
	case BUTT_SW1:
		return SW1_READ;
	case BUTT_SW2:
		return SW2_READ;
	default:
		return 0;
	}
}

#if ( BUTTONS_SUPPORT_LONG == 0)
static void button_proc(void) {
	int n;
	for (n=0; n<MAX_BUTT; n++) {
		if (button_state(1<<n)) {
			if (last_state[n] < -4) { // ������ �� ���������� ������� (�� ������� ��� ����� 50��)
				last_state[n] = 0;
			}
			last_state[n]++;
			if (last_state[n] == 3) { // ������ ��������� � ������� 30��.
				state |= (1<<n);
			} else 
			if (last_state[n] > 100) { // ������ ������� ���������� �������
				if (last_state[n]%15 == 0) // ����������, ������ ������ �� ���� 150��
					state |= (1<<n);
			}
		} else {
			if (last_state[n] > 0)
				last_state[n] = 0;
			last_state[n]--;
		}
	}
	event_set(button_proc, 10);
}
#else
static void button_proc(void) {
	int n;
	for (n=0; n<MAX_BUTT; n++) {
		if (button_state(1<<(n*2))) {
			if (last_state[n] < -4) { // ������ �� ���������� ������� (�� ������� ��� ����� 50��)
				last_state[n] = 0;
			}
			if (last_state[n] > 1000) {
				// nop
			} else
			if (last_state[n] > 100) { // ������ ������� - ����. �������
				state |= (2<<(n*2));
				last_state[n] = 1000;
			}
			last_state[n]++;
		} else {
			if (last_state[n] > 100) {
				// nop
			} else
			if (last_state[n] > 4) { // 40�� - �������� �������
				state |= (1<<(n*2));
			}
			if (last_state[n] > 0) {
				last_state[n] = 0;
			}
			last_state[n]--;
		}
	}
	event_set(button_proc, 10);
}
#endif

void button_init(void) {
	state = 0;
	memset(last_state, 0x00, sizeof(last_state));
	event_set(button_proc, 10);
}

int button_read(void) {
	int ret;
	ret = state; 
	state = 0; // � ����������� ������������, ���������� ����������� �������� �������.
	return ret;
}
