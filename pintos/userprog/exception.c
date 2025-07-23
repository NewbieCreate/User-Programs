#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 유저 프로그램에 의해 발생할 수 있는 인터럽트 핸들러들을 등록합니다.

   실제 유닉스 계열 운영체제에서는, 대부분의 이런 인터럽트들이 [SV-386] 3-24와 3-25에
   설명된 것처럼 시그널(signal) 형태로 유저 프로세스에 전달됩니다.
   하지만 우리는 시그널을 구현하지 않습니다. 대신, 간단하게 유저 프로세스를
   종료시키도록 만들 것입니다.

   페이지 폴트는 예외입니다. 여기서는 다른 예외들과 동일한 방식으로 처리되지만,
   가상 메모리를 구현하기 위해서는 이 부분을 변경해야 합니다.

   각 예외에 대한 설명은 [IA32-v3a] 섹션 5.15 "Exception and Interrupt
   Reference"를 참조하세요. */
void
exception_init (void) {
	/* 아래의 예외들은 유저 프로그램에 의해 명시적으로 발생할 수 있습니다.
       (예: INT, INT3, INTO, BOUND 명령어 사용). 따라서,
       DPL(Descriptor Privilege Level)을 3으로 설정하여 유저 프로그램이
       이 명령어들을 통해 해당 인터럽트를 호출하는 것을 허용합니다. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 아래의 예외들은 DPL==0으로 설정되어, 유저 프로세스가 INT 명령어로
       이들을 호출하는 것을 막습니다. 하지만 간접적으로는 발생할 수 있습니다.
       (예: 0으로 나누면 #DE 예외 발생) */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트가 켜진 상태에서 처리될 수 있습니다.
       하지만 페이지 폴트의 경우, 폴트 주소가 CR2 레지스터에 저장되므로
       그 값을 보존하기 위해 인터럽트를 비활성화해야 합니다. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) {
	/* This interrupt is one (probably) caused by a user process.
	   For example, the process might have tried to access unmapped
	   virtual memory (a page fault).  For now, we simply kill the
	   user process.  Later, we'll want to handle page faults in
	   the kernel.  Real Unix-like operating systems pass most
	   exceptions back to the process via signals, but we don't
	   implement them. */

	/* The interrupt frame's code segment value tells us where the
	   exception originated. */
	switch (f->cs) {
		case SEL_UCSEG:
			/* User's code segment, so it's a user exception, as we
			   expected.  Kill the user process.  */
			printf ("%s: dying due to interrupt %#04llx (%s).\n",
					thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
			/* Kernel's code segment, which indicates a kernel bug.
			   Kernel code shouldn't throw exceptions.  (Page faults
			   may cause kernel exceptions--but they shouldn't arrive
			   here.)  Panic the kernel to make the point.  */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* Some other code segment?  Shouldn't happen.  Panic the
			   kernel. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* 페이지 폴트 핸들러. 이 코드는 가상 메모리를 구현하기 위해 채워져야 하는
   기본 골격입니다. 프로젝트 2의 일부 해결책은 이 코드를
   수정해야 할 수도 있습니다.

   핸들러 진입 시, 폴트를 발생시킨 주소는 CR2 (Control Register 2)에 저장되어 있고,
   폴트에 대한 정보(exception.h의 PF_* 매크로에 설명된 형식)는
   f->error_code 멤버에 있습니다. 여기 예제 코드는 해당 정보를
   분석하는 방법을 보여줍니다. 이 두 가지에 대한 더 자세한 정보는
   [IA32-v3a] 섹션 5.15 "Exception and Interrupt Reference"의
   "Interrupt 14--Page Fault Exception (#PF)" 설명에서 찾을 수 있습니다. */
static void
page_fault (struct intr_frame *f) {
    void *fault_addr = (void *) rcr2();  // 폴트 발생 주소
    intr_enable();  // 인터럽트 재허용

    bool not_present = (f->error_code & PF_P) == 0;  // 페이지 없음
    bool write = (f->error_code & PF_W) != 0;        // 쓰기 접근
    bool user = (f->error_code & PF_U) != 0;         // 사용자 접근

    // 커널이 유저 주소에 접근해서 page fault 발생한 경우: 시스템콜 안에서 포인터 확인 안 했다는 뜻
    if (!user && is_user_vaddr(fault_addr)) {
        thread_exit();
    }

#ifdef VM
    if (vm_try_handle_fault(f, fault_addr, user, write, not_present))
        return;
#endif

    page_fault_cnt++;

    // 유저 프로세스가 잘못된 주소 접근 → 테스트 요구에 따라 exit(-1)
    if (user) {
        exit(-1);  // 이걸 꼭 호출해야 bad-read 같은 테스트가 통과함
    }

    // 커널 모드 오류면 PANIC
    printf ("Page fault at %p: %s error %s page in %s context.\n",
            fault_addr,
            not_present ? "not present" : "rights violation",
            write ? "writing" : "reading",
            user ? "user" : "kernel");
    kill(f);  // 최후 수단
}


