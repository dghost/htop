/*
htop - dragonflybsd/DragonFlyBSDProcess.c
(C) 2015 Hisham H. Muhammad
(C) 2017 Diederik de Groot
Released under the GNU GPLv2, see the COPYING file
in the source distribution for its full text.
*/

#include "Process.h"
#include "ProcessList.h"
#include "DragonFlyBSDProcess.h"
#include "Platform.h"
#include "CRT.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>


const ProcessFieldData Process_fields[LAST_PROCESSFIELD] = {
   [0] = { .name = "", .title = NULL, .description = NULL, .flags = 0, },
   [PID] = { .name = "PID", .title = "PID", .description = "Process/thread ID", .flags = 0, .pidColumn = true, },
   [COMM] = { .name = "Command", .title = "Command ", .description = "Command line", .flags = 0, },
   [STATE] = { .name = "STATE", .title = "S ", .description = "Process state (S sleeping (<20s), I Idle, Q Queued for Run, R running, D disk, Z zombie, T traced, W paging, B Blocked, A AskedPage, C Core, J Jailed)", .flags = 0, },
   [PPID] = { .name = "PPID", .title = "PPID", .description = "Parent process ID", .flags = 0, .pidColumn = true, },
   [PGRP] = { .name = "PGRP", .title = "PGRP", .description = "Process group ID", .flags = 0, .pidColumn = true, },
   [SESSION] = { .name = "SESSION", .title = "SID", .description = "Process's session ID", .flags = 0, .pidColumn = true, },
   [TTY_NR] = { .name = "TTY_NR", .title = "    TTY ", .description = "Controlling terminal", .flags = 0, },
   [TPGID] = { .name = "TPGID", .title = "TPGID", .description = "Process ID of the fg process group of the controlling terminal", .flags = 0, .pidColumn = true, },
   [MINFLT] = { .name = "MINFLT", .title = "     MINFLT ", .description = "Number of minor faults which have not required loading a memory page from disk", .flags = 0, },
   [MAJFLT] = { .name = "MAJFLT", .title = "     MAJFLT ", .description = "Number of major faults which have required loading a memory page from disk", .flags = 0, },
   [PRIORITY] = { .name = "PRIORITY", .title = "PRI ", .description = "Kernel's internal priority for the process", .flags = 0, },
   [NICE] = { .name = "NICE", .title = " NI ", .description = "Nice value (the higher the value, the more it lets other processes take priority)", .flags = 0, },
   [STARTTIME] = { .name = "STARTTIME", .title = "START ", .description = "Time the process was started", .flags = 0, },
   [PROCESSOR] = { .name = "PROCESSOR", .title = "CPU ", .description = "Id of the CPU the process last executed on", .flags = 0, },
   [M_VIRT] = { .name = "M_VIRT", .title = " VIRT ", .description = "Total program size in virtual memory", .flags = 0, },
   [M_RESIDENT] = { .name = "M_RESIDENT", .title = "  RES ", .description = "Resident set size, size of the text and data sections, plus stack usage", .flags = 0, },
   [ST_UID] = { .name = "ST_UID", .title = "  UID ", .description = "User ID of the process owner", .flags = 0, },
   [PERCENT_CPU] = { .name = "PERCENT_CPU", .title = "CPU% ", .description = "Percentage of the CPU time the process used in the last sampling", .flags = 0, },
   [PERCENT_NORM_CPU] = { .name = "PERCENT_NORM_CPU", .title = "NCPU%", .description = "Normalized percentage of the CPU time the process used in the last sampling (normalized by cpu count)", .flags = 0, },
   [PERCENT_MEM] = { .name = "PERCENT_MEM", .title = "MEM% ", .description = "Percentage of the memory the process is using, based on resident memory size", .flags = 0, },
   [USER] = { .name = "USER", .title = "USER      ", .description = "Username of the process owner (or user ID if name cannot be determined)", .flags = 0, },
   [TIME] = { .name = "TIME", .title = "  TIME+  ", .description = "Total time the process has spent in user and system time", .flags = 0, },
   [NLWP] = { .name = "NLWP", .title = "NLWP ", .description = "Number of threads in the process", .flags = 0, },
   [TGID] = { .name = "TGID", .title = "TGID", .description = "Thread group ID (i.e. process ID)", .flags = 0, .pidColumn = true, },
   [JID] = { .name = "JID", .title = "JID", .description = "Jail prison ID", .flags = 0, .pidColumn = true, },
   [JAIL] = { .name = "JAIL", .title = "JAIL        ", .description = "Jail prison name", .flags = 0, },
};

Process* DragonFlyBSDProcess_new(const Settings* settings) {
   DragonFlyBSDProcess* this = xCalloc(1, sizeof(DragonFlyBSDProcess));
   Object_setClass(this, Class(DragonFlyBSDProcess));
   Process_init(&this->super, settings);
   return &this->super;
}

void Process_delete(Object* cast) {
   DragonFlyBSDProcess* this = (DragonFlyBSDProcess*) cast;
   Process_done((Process*)cast);
   free(this->jname);
   free(this);
}

static void DragonFlyBSDProcess_writeField(const Process* this, RichString* str, ProcessField field) {
   const DragonFlyBSDProcess* fp = (const DragonFlyBSDProcess*) this;
   char buffer[256]; buffer[255] = '\0';
   int attr = CRT_colors[DEFAULT_COLOR];
   size_t n = sizeof(buffer) - 1;
   switch (field) {
   // add Platform-specific fields here
   case PID: xSnprintf(buffer, n, "%*d ", Process_pidDigits, (fp->kernel ? -1 : this->pid)); break;
   case JID: xSnprintf(buffer, n, "%*d ", Process_pidDigits, fp->jid); break;
   case JAIL: {
      xSnprintf(buffer, n, "%-11s ", fp->jname);
      if (buffer[11] != '\0') {
         buffer[11] = ' ';
         buffer[12] = '\0';
      }
      break;
   }
   default:
      Process_writeField(this, str, field);
      return;
   }
   RichString_appendWide(str, attr, buffer);
}

static int DragonFlyBSDProcess_compareByKey(const Process* v1, const Process* v2, ProcessField key) {
   const DragonFlyBSDProcess* p1 = (const DragonFlyBSDProcess*)v1;
   const DragonFlyBSDProcess* p2 = (const DragonFlyBSDProcess*)v2;

   switch (key) {
   // add Platform-specific fields here
   case JID:
      return SPACESHIP_NUMBER(p1->jid, p2->jid);
   case JAIL:
      return SPACESHIP_NULLSTR(p1->jname, p2->jname);
   default:
      return Process_compareByKey_Base(v1, v2, key);
   }
}

bool Process_isThread(const Process* this) {
   const DragonFlyBSDProcess* fp = (const DragonFlyBSDProcess*) this;

   if (fp->kernel == 1 ) {
      return 1;
   } else {
      return (Process_isUserlandThread(this));
   }
}

const ProcessClass DragonFlyBSDProcess_class = {
   .super = {
      .extends = Class(Process),
      .display = Process_display,
      .delete = Process_delete,
      .compare = Process_compare
   },
   .writeField = DragonFlyBSDProcess_writeField,
   .compareByKey = DragonFlyBSDProcess_compareByKey
};
