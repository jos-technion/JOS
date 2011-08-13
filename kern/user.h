#ifndef JOS_INC_USER_H
#define JOS_INC_USER_H


#define NUSERS			10
#define MAX_USER_GROUPS		10
#define MAX_USER_LENGTH		20
#define MAX_PATH_LENGTH		255


extern struct User *users;

void user_init(void);

struct User {
	int is_initialized;
	char user[MAX_USER_LENGTH];
	int groups[MAX_USER_GROUPS];
	char path[MAX_PATH_LENGTH];
};

#endif
