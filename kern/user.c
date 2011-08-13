#include <kern/user.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

struct User *users = NULL;



void
user_init() {
	int i=0;
	for(i = 0; i < NUSERS ; i++) {
		users[i].is_initialized = 0;
		users[i].path[0] = '/';
		users[i].path[1] = '\0';
	}
}

int
user_alloc(char* name, char* passwd) {
	int i ;
	for(i = 0 ; i < NUSERS ; i++) {
		if(users[i].is_initialized == 0) 
			break;
	}
	if (i == NUSERS)
		return -1;
	return 0;
	
}

int
login(char* name, char* passwd) {
	return 0;
}
