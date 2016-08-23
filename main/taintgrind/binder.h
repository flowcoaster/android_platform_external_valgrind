#ifndef BINDER_H
#define BINDER_H

#include "pub_tool_vki.h" // the remaining binder stuff

struct vki_binder_pri_ptr_cookie {
	int priority;
	void *ptr;
	void *cookie;
};

struct vki_binder_ptr_cookie {
	void *ptr;
	void *cookie;
};

struct vki_binder_pri_desc {
	int priority;
	int desc;
};

struct vki_binder_transaction_data {
	/* The first two are only used for bcTRANSACTION and brTRANSACTION,
	 * identifying the target and contents of the transaction.
	 */
	union {
		vki_size_t	handle;	/* target descriptor of command transaction */
		void	*ptr;	/* target descriptor of return transaction */
	} target;
	void		*cookie;	/* target object cookie */
	unsigned int	code;		/* transaction command */

	/* General information about the transaction. */
	unsigned int	flags;
	vki_pid_t		sender_pid;
	vki_uid_t		sender_euid;
	vki_size_t		data_size;	/* number of bytes of data */
	vki_size_t		offsets_size;	/* number of bytes of offsets */

	/* If this transaction is inline, the data immediately
	 * follows here; otherwise, it ends with a pointer to
	 * the data buffer.
	 */
	//union {
	//	struct {
			/* transaction data */
			//const void	*buffer;
			/* offsets from buffer to flat_binder_object structs */
			//const void	*offsets;
	//	} ptr;
	//	vki_uint8_t	buf[8];
	//} data;
	void *data;
	void *offs;

};

/* The _IO?_BAD() macros required so that these evaluate to a
 * constant expression, otherwise this fails to compile in C++
 */
#define VKI_BINDER_BR_ERROR _VKI_IOR_BAD('r', 0, int)
#define VKI_BINDER_BR_OK _VKI_IO('r', 1)
#define VKI_BINDER_BR_TRANSACTION _VKI_IOR_BAD('r', 2, struct vki_binder_transaction_data)
#define VKI_BINDER_BR_REPLY _VKI_IOR_BAD('r', 3, struct vki_binder_transaction_data)
#define VKI_BINDER_BR_ACQUIRE_RESULT _VKI_IOR_BAD('r', 4, int)
#define VKI_BINDER_BR_DEAD_REPLY _VKI_IO('r', 5)
#define VKI_BINDER_BR_TRANSACTION_COMPLETE _VKI_IO('r', 6)
#define VKI_BINDER_BR_INCREFS _VKI_IOR_BAD('r', 7, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BR_ACQUIRE _VKI_IOR_BAD('r', 8, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BR_RELEASE _VKI_IOR_BAD('r', 9, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BR_DECREFS _VKI_IOR_BAD('r', 10, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BR_ATTEMPT_ACQUIRE _VKI_IOR_BAD('r', 11, struct vki_binder_pri_ptr_cookie)
#define VKI_BINDER_BR_NOOP _VKI_IO('r', 12)
#define VKI_BINDER_BR_SPAWN_LOOPER _VKI_IO('r', 13)
#define VKI_BINDER_BR_FINISHED _VKI_IO('r', 14)
#define VKI_BINDER_BR_DEAD_BINDER _VKI_IOR_BAD('r', 15, void *)
#define VKI_BINDER_BR_CLEAR_DEATH_NOTIFICATION_DONE _VKI_IOR_BAD('r', 16, void *)
#define VKI_BINDER_BR_DEAD_BINDER _VKI_IOR_BAD('r', 15, void *)
#define VKI_BINDER_BR_FAILED_REPLY _VKI_IO('r', 17)

#define VKI_BINDER_BC_TRANSACTION _VKI_IOW_BAD('c', 0, struct vki_binder_transaction_data)
#define VKI_BINDER_BC_REPLY _VKI_IOW_BAD('c', 1, struct vki_binder_transaction_data)
#define VKI_BINDER_BC_ACQUIRE_RESULT _VKI_IOW_BAD('c', 2, int)
#define VKI_BINDER_BC_FREE_BUFFER _VKI_IOW_BAD('c', 3, int)
#define VKI_BINDER_BC_INCREFS _VKI_IOW_BAD('c', 4, int)
#define VKI_BINDER_BC_ACQUIRE _VKI_IOW_BAD('c', 5, int)
#define VKI_BINDER_BC_RELEASE _VKI_IOW_BAD('c', 6, int)
#define VKI_BINDER_BC_DECREFS _VKI_IOW_BAD('c', 7, int)
#define VKI_BINDER_BC_INCREFS_DONE _VKI_IOW_BAD('c', 8, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BC_ACQUIRE_DONE _VKI_IOW_BAD('c', 9, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BC_ATTEMPT_ACQUIRE _VKI_IOW_BAD('c', 10, struct vki_binder_pri_desc)
#define VKI_BINDER_BC_REGISTER_LOOPER _VKI_IO('c', 11)
#define VKI_BINDER_BC_ENTER_LOOPER _VKI_IO('c', 12)
#define VKI_BINDER_BC_EXIT_LOOPER _VKI_IO('c', 13)
#define VKI_BINDER_BC_REQUEST_DEATH_NOTIFICATION _VKI_IOW_BAD('c', 14, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BC_CLEAR_DEATH_NOTIFICATION _VKI_IOW_BAD('c', 15, struct vki_binder_ptr_cookie)
#define VKI_BINDER_BC_DEAD_BINDER_DONE _VKI_IOW_BAD('c', 16, void *)

#endif //ifndef BINDER_H

