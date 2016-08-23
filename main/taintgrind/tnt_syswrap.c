
/*--------------------------------------------------------------------*/
/*--- Wrappers for tainting syscalls                               ---*/
/*---                                                tnt_syswrap.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Taintgrind, a Valgrind tool for
   tracking marked/tainted data through memory.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_vki.h"
#include "pub_tool_vkiscnums.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_machine.h"
#include "pub_tool_aspacemgr.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_stacktrace.h"   // for VG_(get_and_pp_StackTrace)
#include "pub_tool_debuginfo.h"	   // VG_(describe_IP), VG_(get_fnname)

#include "valgrind.h"

#include "tnt_include.h"
#include "binder.h"

static
void resolve_filename(UWord fd, HChar *path, Int max)
{
   HChar src[FD_MAX_PATH];
   Int len = 0;

   // TODO: Cache resolved fds by also catching open()s and close()s
   VG_(sprintf)(src, "/proc/%d/fd/%d", VG_(getpid)(), (int)fd);
   len = VG_(readlink)(src, path, max);

   // Just give emptiness on error.
   if (len == -1) len = 0;
   path[len] = '\0';
}

/* enforce an arbitrary maximum */
static Bool tainted_fds[VG_N_THREADS][FD_MAX];
static UInt read_offset = 0;

void TNT_(syscall_llseek)(ThreadId tid, UWord* args, UInt nArgs,
                                  SysRes res) {
// int  _llseek(int fildes, ulong offset_high, ulong offset_low, loff_t *result,, uint whence);
   Int   fd           = args[0];
   ULong offset_high  = args[1];
   ULong offset_low   = args[2];
   UInt  result       = args[3];
   UInt  whence       = args[4];
   ULong offset;

   if (fd >= FD_MAX || tainted_fds[tid][fd] == False)
      return;

   VG_(printf)("syscall _llseek %d %d ", tid, fd);
   VG_(printf)("0x%x 0x%x 0x%x 0x%x\n", (UInt)offset_high, (UInt)offset_low, result, whence);

   offset = (offset_high<<32) | offset_low;

   if( whence == 0/*SEEK_SET*/ )
      read_offset = 0 + (UInt)offset;
   else if( whence == 1/*SEEK_CUR*/ )
      read_offset += (UInt)offset;
   else //if( whence == 2/*SEEK_END*/ )
      tl_assert(0);
}

Bool TNT_(syscall_allowed_check)(ThreadId tid, int syscallno) {
	if (IN_SANDBOX && IS_SYSCALL_ALLOWED(syscallno)) {
		HChar fnname[FNNAME_MAX];
		TNT_(get_fnname)(tid, fnname, FNNAME_MAX);
		VG_(printf)("*** Sandbox performed system call %s (%d) in method %s, but it is not allowed to. ***\n", syscallnames[syscallno], syscallno, fnname);
		VG_(get_and_pp_StackTrace)(tid, STACK_TRACE_SIZE);
		VG_(printf)("\n");
		return False;
	}
	return True;
}

static
void read_common ( UInt curr_offset, Int curr_len,
                   UInt taint_offset, Int taint_len,
                   HChar *data ) {
   UWord addr;
   Int   len;
   Int   i;

   if( TNT_(clo_taint_all) ){
      addr = (UWord)data;
      len  = curr_len;
   }else

   /* Here we determine what bytes to taint
      We have 4 variables -
      taint_offset    Starting file offset to taint
      taint_len       Number of bytes to taint
      curr_offset     Starting file offset currently read
      curr_len        Number of bytes currently read
      We have to deal with 4 cases: (= refers to the region to be tainted)
      Case 1:
                          taint_len
      taint_offset   |-----------------|
                          curr_len
      curr_offset |---=================---|
      Case 2:
                          taint_len
      taint_offset   |-----------------------|
                          curr_len
      curr_offset |---====================|
      Case 3:
                          taint_len
      taint_offset |----------------------|
                          curr_len
      curr_offset    |====================---|
      Case 4:
                          taint_len
      taint_offset |-----------------------|
                          curr_len
      curr_offset    |====================|
   */

   if( taint_offset >= curr_offset &&
       taint_offset <= curr_offset + curr_len ){
       if( (taint_offset + taint_len) <= (curr_offset + curr_len) ){
         // Case 1
         addr = (UWord)(data + taint_offset - curr_offset);
         len  = taint_len;
      }else{
          // Case 2
          addr = (UWord)(data + taint_offset - curr_offset);
          len  = curr_len - taint_offset + curr_offset;
      }

   }else if( ( ( taint_offset + taint_len ) >= curr_offset ) &&
             ( ( taint_offset + taint_len ) <= (curr_offset + curr_len ) ) ){
      // Case 3
      addr = (UWord)data;
      len  = taint_len - curr_offset + taint_offset;
   }else if( ( taint_offset <= curr_offset ) &&
       ( taint_offset + taint_len ) >= ( curr_offset + curr_len ) ){
      // Case 4
      addr = (UWord)data;
      len  = curr_len;
   }else{
      return;
   }

   TNT_(make_mem_tainted)( addr, len, 0x5555 );

   for( i=0; i<len; i++) 
      VG_(printf)("taint_byte 0x%08lx 0x%x\n", addr+i, *(Char *)(addr+i));
}

void TNT_(syscall_read)(ThreadId tid, UWord* args, UInt nArgs,
                                  SysRes res) {
// ssize_t  read(int fildes, void *buf, size_t nbyte);
   Int   fd           = args[0];
   HChar *data        = (HChar *)args[1];
   UInt  curr_offset  = read_offset;
   Int   curr_len     = sr_Res(res);
   UInt  taint_offset = TNT_(clo_taint_start);
   Int   taint_len    = TNT_(clo_taint_len);

   TNT_(check_fd_access)(tid, fd, FD_READ);

   if (curr_len == 0) return;

   TNT_(make_mem_defined)( (UWord)data, curr_len );

   if (fd >= FD_MAX || tainted_fds[tid][fd] == False)
      return;

   if(1){
      //VG_(printf)("taint_offset: 0x%x\ttaint_len: 0x%x\n", taint_offset, taint_len);
      //VG_(printf)("curr_offset : 0x%x\tcurr_len : 0x%x\n", curr_offset, curr_len);
      VG_(printf)("syscall read %d %d ", tid, fd);
#ifdef VGA_amd64
      VG_(printf)("0x%x 0x%x 0x%llx 0x%x\n", curr_offset, curr_len, (ULong)data,
          *(HChar *)data);
#else
      VG_(printf)("0x%x 0x%x 0x%x 0x%x\n", curr_offset, curr_len, (UInt)data,
          *(HChar *)data);
#endif
   }

   read_common ( taint_offset, taint_len, curr_offset, curr_len, data );

   /* Here we determine what bytes to taint
      We have 4 variables -
      taint_offset    Starting file offset to taint
      taint_len       Number of bytes to taint
      curr_offset     Starting file offset currently read
      curr_len        Number of bytes currently read
      We have to deal with 4 cases: (= refers to the region to be tainted)
      Case 1:
                          taint_len
      taint_offset   |-----------------|
                          curr_len
      curr_offset |---=================---|
      Case 2:
                          taint_len
      taint_offset   |-----------------------|
                          curr_len
      curr_offset |---====================|
      Case 3:
                          taint_len
      taint_offset |----------------------|
                          curr_len
      curr_offset    |====================---|
      Case 4:
                          taint_len
      taint_offset |-----------------------|
                          curr_len
      curr_offset    |====================|
   */


   // Update file position
   read_offset += curr_len;

   // DEBUG
   //tnt_read = 1;
}

void TNT_(syscall_pread)(ThreadId tid, UWord* args, UInt nArgs,
                                  SysRes res) {
// ssize_t pread(int fildes, void *buf, size_t nbyte, size_t offset);
   Int   fd           = args[0];
   HChar *data        = (HChar *)args[1];
   UInt  curr_offset  = (Int)args[3];
   Int   curr_len     = sr_Res(res);
   UInt  taint_offset = TNT_(clo_taint_start);
   Int   taint_len    = TNT_(clo_taint_len);
//   UWord addr;
//   Int   len;
//   Int   i;

   if (curr_len == 0) return;

   TNT_(make_mem_defined)( (UWord)data, curr_len );

   if (fd >= FD_MAX || tainted_fds[tid][fd] == False)
      return;

   if(1){
      //VG_(printf)("taint_offset: 0x%x\ttaint_len: 0x%x\n", taint_offset, taint_len);
      //VG_(printf)("curr_offset : 0x%x\tcurr_len : 0x%x\n", curr_offset, curr_len);
      VG_(printf)("syscall pread %d %d ", tid, fd);

#ifdef VGA_amd64
      VG_(printf)("0x%x 0x%x 0x%llx\n", curr_offset, curr_len, (ULong)data);
#else
      VG_(printf)("0x%x 0x%x 0x%x\n", curr_offset, curr_len, (UInt)data);
#endif

   }

   read_common ( taint_offset, taint_len, curr_offset, curr_len, data );


   /* Here we determine what bytes to taint
      We have 4 variables -
      taint_offset    Starting file offset to taint
      taint_len       Number of bytes to taint
      curr_offset     Starting file offset currently read
      curr_len        Number of bytes currently read
      We have to deal with 4 cases: (= refers to the region to be tainted)
      Case 1:
                          taint_len
      taint_offset   |-----------------|
                          curr_len
      curr_offset |---=================---|
      Case 2:
                          taint_len
      taint_offset   |-----------------------|
                          curr_len
      curr_offset |---====================|
      Case 3:
                          taint_len
      taint_offset |----------------------|
                          curr_len
      curr_offset    |====================---|
      Case 4:
                          taint_len
      taint_offset |-----------------------|
                          curr_len
      curr_offset    |====================|
   */

}


void TNT_(syscall_open)(ThreadId tid, UWord* args, UInt nArgs, SysRes res) {
//  int open (const char *filename, int flags[, mode_t mode])
   HChar fdpath[FD_MAX_PATH];
   Int fd = sr_Res(res);

   // check if we have already created a sandbox
   if (have_created_sandbox && !IN_SANDBOX) {
#ifdef VGO_freebsd
	   VG_(resolve_filename)(fd, fdpath, FD_MAX_PATH-1);
#elif defined VGO_linux
	   resolve_filename(fd, fdpath, FD_MAX_PATH-1);
#else
#error OS unknown
#endif
	   HChar fnname[FNNAME_MAX];
	   TNT_(get_fnname)(tid, fnname, FNNAME_MAX);
	   VG_(printf)("*** The file %s (fd: %d) was opened in method %s after a sandbox was created, hence it will not be accessible to the sandbox. It will need to be passed to the sandbox using sendmsg. ***\n", fdpath, fd, fnname);
	   VG_(get_and_pp_StackTrace)(tid, STACK_TRACE_SIZE);
	   VG_(printf)("\n");
   }

    // Nothing to do if no file tainting
    if ( VG_(strlen)( TNT_(clo_file_filter)) == 0 )
        return;

    if (fd > -1 && fd < FD_MAX) {

#ifdef VGO_freebsd
	VG_(resolve_filename)(fd, fdpath, FD_MAX_PATH-1);
#elif defined VGO_linux
        resolve_filename(fd, fdpath, FD_MAX_PATH-1);
#else
#error OS unknown
#endif

        if( TNT_(clo_taint_all) ){

            tainted_fds[tid][fd] = True;
            VG_(printf)("syscall open %d %s %lx %d\n", tid, fdpath, args[1], fd);
            read_offset = 0;

        } else if ( VG_(strncmp)(fdpath, TNT_(clo_file_filter), 
                            VG_(strlen)( TNT_(clo_file_filter))) == 0 ) {

            tainted_fds[tid][fd] = True;
            VG_(printf)("syscall open %d %s %lx %d\n", tid, fdpath, args[1], fd);
            read_offset = 0;

        } else if ( TNT_(clo_file_filter)[0] == '*' &&
            VG_(strncmp)( fdpath + VG_(strlen)(fdpath) 
                        - VG_(strlen)( TNT_(clo_file_filter) ) + 1, 
                          TNT_(clo_file_filter) + 1, 
                          VG_(strlen)( TNT_(clo_file_filter)) - 1 ) == 0 ) {

            tainted_fds[tid][fd] = True;
            VG_(printf)("syscall open %d %s %lx %d\n", tid, fdpath, args[1], fd);
            read_offset = 0;
        } else
            tainted_fds[tid][fd] = False;
    }
}

void TNT_(syscall_close)(ThreadId tid, UWord* args, UInt nArgs, SysRes res) {
//   int close (int filedes)
   Int fd = args[0];

   if (fd > -1 && fd < FD_MAX){
     if (tainted_fds[tid][fd] == True)
         VG_(printf)("syscall close %d %d\n", tid, fd);

     shared_fds[fd] = 0;
     tainted_fds[tid][fd] = False;
   }
}

void TNT_(syscall_write)(ThreadId tid, UWord* args, UInt nArgs, SysRes res) {
	Int fd = args[0];
	TNT_(check_fd_access)(tid, fd, FD_WRITE);
}

#ifdef VERBOSE_DEBUG
void TNT_(ppBinderTransactionData)(struct vki_binder_transaction_data* data) {
	VG_(printf)("tnt_ppBinderTransactionData(data=%p)\n", data);
	VG_(printf)("cookie=%p\n", data->cookie);
	VG_(printf)("target=%p\n", data->target);
	VG_(printf)("transaction command=%d\n", data->code);
	VG_(printf)("flags=%p\n", data->flags);
	VG_(printf)("sender pid=%lu\n", data->sender_pid);
	VG_(printf)("sender euid=%lu\n", data->sender_euid);
	vki_size_t size = data->data_size;
	VG_(printf)("data size=%d\n", size);
	VG_(printf)("offsets size=%d\n", data->offsets_size);
	UInt *raw = (UInt*) data->data;
	VG_(printf)("data=%p\n", raw);
	VG_(printf)("offsets=%p\n", data->offs);
	while (size > 0) {
		UInt current = *raw++;
		VG_(printf)("%p:%d\n", current, current);
		size -= sizeof(raw);
	}
}
#endif

void TNT_(taintIncoming)(struct vki_binder_transaction_data* data) {
	if (data->code == 4) { // code 4 -> CALL
		UInt *raw = (UInt*) data->data;
#ifdef VERBOSE_DEBUG
		UInt strictPolicy = *raw++;
#else
		raw++;
#endif
		UInt lenInterfaceName = *raw++;
		if (lenInterfaceName % 2 == 1) lenInterfaceName++;
		raw += lenInterfaceName / 2;
		UInt argc = *raw++;
#ifdef VERBOSE_DEBUG
		VG_(printf)("strictPolicy=%d\n", strictPolicy);
		VG_(printf)("length of Interface descriptor string = %d\n", lenInterfaceName);
		VG_(printf)("argc = %d\n", argc);
#endif
		if (argc > 10) return;
		UInt taint, param, i=0;
		for (i=0; i<argc; i++) {
			taint = *raw;
			if (taint != 0) {
				param = *(raw + argc);
#ifdef VERBOSE_DEBUG
				VG_(printf)("Tainting memory: %p, size %d\n",
						raw+argc, sizeof(param));
				VG_(printf)("tainted param = %d ", param);
#endif
				TNT_(make_mem_tainted)(raw+argc, sizeof(param), taint);
			}
#ifdef VERBOSE_DEBUG
			VG_(printf)("taint=%d\n", taint);
#endif
			raw++;
		}
		for (i=0; i<argc; i++) {
			param = *raw++;
#ifdef VERBOSE_DEBUG
			VG_(printf)("param=%d\n", param);
#endif
		}

		//TNT_(make_mem_tainted)(data->data, data->data_size);
	}
}

Bool TNT_(isCallReply)(struct vki_binder_transaction_data* data) {
	UInt size = data->data_size/4 - 2;
	UInt* raw = data->data;
	raw++; raw++;
#ifdef VERBOSE_DEBUG
	VG_(printf)("isCallReply: starting at %p, size=%d\n", raw, size);
#endif
	while (size > 0) {
		if (*raw != 0 || TNT_(get_taint)((Addr)raw) != 0) {
#ifdef VERBOSE_DEBUG
			VG_(printf("is not a call reply because raw=%p->*raw=%d and taint=%lx\n", raw, *raw, TNT_(get_taint)((Addr)raw)));
#endif
			return 0;
		}
		size--;
		raw++;
	}
#ifdef VERBOSE_DEBUG
	VG_(printf)("this is a call reply\n");
#endif
	return 1;
}

UInt TNT_(getTaintForOutgoing)(struct vki_binder_transaction_data* data) {
	UInt size = data->data_size/4;
	UInt taint = 0;
	UInt* raw = data->data;
#ifdef VERBOSE_DEBUG
	VG_(printf)("getTaintForOutgoing: starting at %p, size=%d\n", raw, size);
#endif
	while (size > 0) {
		taint = TNT_(get_taint)((Addr)raw);
#ifdef VERBOSE_DEBUG
		VG_(printf)("taint for %p->%d is %x\n", raw, *raw, taint);
#endif
		raw++; size --;
	}
	if (TNT_(isCallReply)(data)) {
		UInt* taintField = data->data; taintField++; taintField++;
		UWord taintValue = TNT_(get_taint)((Addr)data->data);
		*taintField = taintValue;
#ifdef VERBOSE_DEBUG
		VG_(printf)("set taint field in reply parcel to %lx\n", taintValue);
#endif
	}
	return taint;
}

void TNT_(syscall_ioctl)(ThreadId tid, UWord* args, UInt nArgs, SysRes res) {
	if (args[1] == VKI_BINDER_WRITE_READ) {
#ifdef VERBOSE_DEBUG
		VG_(printf)("-> Thread %d: post syscall_ioctl with binder write/read\n", tid);
#endif
		struct vki_binder_write_read* bwr = (struct vki_binder_write_read*)args[2];
		UInt size = bwr->read_size;
		UInt *writebuf = (UInt*) bwr->read_buffer;
		UInt *end = writebuf + (size / sizeof(UInt));
		while (writebuf < end) {
			UInt cmd = *writebuf++;
			switch ((int)cmd) {
			case VKI_BINDER_BR_TRANSACTION:
#ifdef VERBOSE_DEBUG
				TNT_(ppBinderTransactionData)((struct vki_binder_transaction_data*) writebuf);
#endif
				TNT_(taintIncoming)((struct vki_binder_transaction_data*) writebuf);
				writebuf += sizeof((struct vki_binder_transaction_data*) writebuf) / sizeof(UInt);
				break;
			case VKI_BINDER_BR_REPLY:
#ifdef VERBOSE_DEBUG
				TNT_(ppBinderTransactionData)((struct vki_binder_transaction_data*) writebuf);
#endif
				TNT_(taintIncoming)((struct vki_binder_transaction_data*) writebuf);
				writebuf += sizeof((struct vki_binder_transaction_data*) writebuf) / sizeof(UInt);
				break;
			}
		}
		size = bwr->write_size;
		writebuf = (UInt*) bwr->write_buffer;
		end = writebuf + (size / 4);
		while (writebuf < end) {
			UInt cmd = *writebuf++;
			switch ((int)cmd) {
			case VKI_BINDER_BC_TRANSACTION:
#ifdef VERBOSE_DEBUG
				TNT_(ppBinderTransactionData)((struct vki_binder_transaction_data*) writebuf);
#endif
				writebuf += sizeof((struct vki_binder_transaction_data*)writebuf) / sizeof(UInt);
				break;
			case VKI_BINDER_BC_REPLY:
				TNT_(getTaintForOutgoing)((struct vki_binder_transaction_data*) writebuf);
#ifdef VERBOSE_DEBUG
				TNT_(ppBinderTransactionData)((struct vki_binder_transaction_data*) writebuf);
#endif

				writebuf += sizeof((struct vki_binder_transaction_data*)writebuf) / sizeof(UInt);
				break;
			}
		}
#ifdef VERBOSE_DEBUG
		VG_(printf)("<- Thread %d: post syscall_ioctl with binder write/read\n", tid);
#endif
	}
}


void TNT_(get_fnname)(ThreadId tid, HChar* buf, UInt buf_size) {
	   UInt pc = VG_(get_IP)(tid);
	   VG_(get_fnname)(pc, buf, buf_size);
}

void TNT_(check_fd_access)(ThreadId tid, UInt fd, Int fd_request) {
	if (IN_SANDBOX) {
		Bool allowed = shared_fds[fd] & fd_request;
//		VG_(printf)("checking if allowed to %s from fd %d ... %d\n", (fd_request == FD_READ ? "read" : "write"), fd, allowed);
		if (!allowed) {
			const HChar* access_str;
			switch (fd_request) {
				case FD_READ: {
					access_str = "read from";
					break;
				}
				case FD_WRITE: {
					access_str = "wrote to";
					break;
				}
				default: {
					tl_assert(0);
					break;
				}
			}
			HChar fdpath[FD_MAX_PATH];
#ifdef VGO_freebsd
			VG_(resolve_filename)(fd, fdpath, FD_MAX_PATH-1);
#elif defined VGO_linux
			resolve_filename(fd, fdpath, FD_MAX_PATH-1);
#else
#error OS unknown
#endif
			HChar fnname[FNNAME_MAX];
			TNT_(get_fnname)(tid, fnname, FNNAME_MAX);
			VG_(printf)("*** Sandbox %s %s (fd: %d) in method %s, but it is not allowed to. ***\n", access_str, fdpath, fd, fnname);
			VG_(get_and_pp_StackTrace)(tid, STACK_TRACE_SIZE);
			VG_(printf)("\n");
		}
	}
}

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
