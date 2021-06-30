/* $Id$ */
/** @file
 * HM - Internal header file.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef VMM_INCLUDED_SRC_include_HMInternal_h
#define VMM_INCLUDED_SRC_include_HMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/stam.h>
#include <VBox/dis.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/hm_vmx.h>
#include <VBox/vmm/hm_svm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/trpm.h>
#include <iprt/memobj.h>
#include <iprt/cpuset.h>
#include <iprt/mp.h>
#include <iprt/avl.h>
#include <iprt/string.h>

#if HC_ARCH_BITS == 32
# error "32-bit hosts are no longer supported. Go back to 6.0 or earlier!"
#endif

/** @def HM_PROFILE_EXIT_DISPATCH
 * Enables profiling of the VM exit handler dispatching. */
#if 0 || defined(DOXYGEN_RUNNING)
# define HM_PROFILE_EXIT_DISPATCH
#endif

RT_C_DECLS_BEGIN


/** @defgroup grp_hm_int       Internal
 * @ingroup grp_hm
 * @internal
 * @{
 */

/** @name HM_CHANGED_XXX
 * HM CPU-context changed flags.
 *
 * These flags are used to keep track of which registers and state has been
 * modified since they were imported back into the guest-CPU context.
 *
 * @{
 */
#define HM_CHANGED_HOST_CONTEXT                  UINT64_C(0x0000000000000001)
#define HM_CHANGED_GUEST_RIP                     UINT64_C(0x0000000000000004)
#define HM_CHANGED_GUEST_RFLAGS                  UINT64_C(0x0000000000000008)

#define HM_CHANGED_GUEST_RAX                     UINT64_C(0x0000000000000010)
#define HM_CHANGED_GUEST_RCX                     UINT64_C(0x0000000000000020)
#define HM_CHANGED_GUEST_RDX                     UINT64_C(0x0000000000000040)
#define HM_CHANGED_GUEST_RBX                     UINT64_C(0x0000000000000080)
#define HM_CHANGED_GUEST_RSP                     UINT64_C(0x0000000000000100)
#define HM_CHANGED_GUEST_RBP                     UINT64_C(0x0000000000000200)
#define HM_CHANGED_GUEST_RSI                     UINT64_C(0x0000000000000400)
#define HM_CHANGED_GUEST_RDI                     UINT64_C(0x0000000000000800)
#define HM_CHANGED_GUEST_R8_R15                  UINT64_C(0x0000000000001000)
#define HM_CHANGED_GUEST_GPRS_MASK               UINT64_C(0x0000000000001ff0)

#define HM_CHANGED_GUEST_ES                      UINT64_C(0x0000000000002000)
#define HM_CHANGED_GUEST_CS                      UINT64_C(0x0000000000004000)
#define HM_CHANGED_GUEST_SS                      UINT64_C(0x0000000000008000)
#define HM_CHANGED_GUEST_DS                      UINT64_C(0x0000000000010000)
#define HM_CHANGED_GUEST_FS                      UINT64_C(0x0000000000020000)
#define HM_CHANGED_GUEST_GS                      UINT64_C(0x0000000000040000)
#define HM_CHANGED_GUEST_SREG_MASK               UINT64_C(0x000000000007e000)

#define HM_CHANGED_GUEST_GDTR                    UINT64_C(0x0000000000080000)
#define HM_CHANGED_GUEST_IDTR                    UINT64_C(0x0000000000100000)
#define HM_CHANGED_GUEST_LDTR                    UINT64_C(0x0000000000200000)
#define HM_CHANGED_GUEST_TR                      UINT64_C(0x0000000000400000)
#define HM_CHANGED_GUEST_TABLE_MASK              UINT64_C(0x0000000000780000)

#define HM_CHANGED_GUEST_CR0                     UINT64_C(0x0000000000800000)
#define HM_CHANGED_GUEST_CR2                     UINT64_C(0x0000000001000000)
#define HM_CHANGED_GUEST_CR3                     UINT64_C(0x0000000002000000)
#define HM_CHANGED_GUEST_CR4                     UINT64_C(0x0000000004000000)
#define HM_CHANGED_GUEST_CR_MASK                 UINT64_C(0x0000000007800000)

#define HM_CHANGED_GUEST_APIC_TPR                UINT64_C(0x0000000008000000)
#define HM_CHANGED_GUEST_EFER_MSR                UINT64_C(0x0000000010000000)

#define HM_CHANGED_GUEST_DR0_DR3                 UINT64_C(0x0000000020000000)
#define HM_CHANGED_GUEST_DR6                     UINT64_C(0x0000000040000000)
#define HM_CHANGED_GUEST_DR7                     UINT64_C(0x0000000080000000)
#define HM_CHANGED_GUEST_DR_MASK                 UINT64_C(0x00000000e0000000)

#define HM_CHANGED_GUEST_X87                     UINT64_C(0x0000000100000000)
#define HM_CHANGED_GUEST_SSE_AVX                 UINT64_C(0x0000000200000000)
#define HM_CHANGED_GUEST_OTHER_XSAVE             UINT64_C(0x0000000400000000)
#define HM_CHANGED_GUEST_XCRx                    UINT64_C(0x0000000800000000)

#define HM_CHANGED_GUEST_KERNEL_GS_BASE          UINT64_C(0x0000001000000000)
#define HM_CHANGED_GUEST_SYSCALL_MSRS            UINT64_C(0x0000002000000000)
#define HM_CHANGED_GUEST_SYSENTER_CS_MSR         UINT64_C(0x0000004000000000)
#define HM_CHANGED_GUEST_SYSENTER_EIP_MSR        UINT64_C(0x0000008000000000)
#define HM_CHANGED_GUEST_SYSENTER_ESP_MSR        UINT64_C(0x0000010000000000)
#define HM_CHANGED_GUEST_SYSENTER_MSR_MASK       UINT64_C(0x000001c000000000)
#define HM_CHANGED_GUEST_TSC_AUX                 UINT64_C(0x0000020000000000)
#define HM_CHANGED_GUEST_OTHER_MSRS              UINT64_C(0x0000040000000000)
#define HM_CHANGED_GUEST_ALL_MSRS                (  HM_CHANGED_GUEST_EFER              \
                                                  | HM_CHANGED_GUEST_KERNEL_GS_BASE    \
                                                  | HM_CHANGED_GUEST_SYSCALL_MSRS      \
                                                  | HM_CHANGED_GUEST_SYSENTER_MSR_MASK \
                                                  | HM_CHANGED_GUEST_TSC_AUX           \
                                                  | HM_CHANGED_GUEST_OTHER_MSRS)

#define HM_CHANGED_GUEST_HWVIRT                  UINT64_C(0x0000080000000000)
#define HM_CHANGED_GUEST_MASK                    UINT64_C(0x00000ffffffffffc)

#define HM_CHANGED_KEEPER_STATE_MASK             UINT64_C(0xffff000000000000)

#define HM_CHANGED_VMX_XCPT_INTERCEPTS           UINT64_C(0x0001000000000000)
#define HM_CHANGED_VMX_GUEST_AUTO_MSRS           UINT64_C(0x0002000000000000)
#define HM_CHANGED_VMX_GUEST_LAZY_MSRS           UINT64_C(0x0004000000000000)
#define HM_CHANGED_VMX_ENTRY_EXIT_CTLS           UINT64_C(0x0008000000000000)
#define HM_CHANGED_VMX_MASK                      UINT64_C(0x000f000000000000)
#define HM_CHANGED_VMX_HOST_GUEST_SHARED_STATE   (  HM_CHANGED_GUEST_DR_MASK \
                                                  | HM_CHANGED_VMX_GUEST_LAZY_MSRS)

#define HM_CHANGED_SVM_XCPT_INTERCEPTS           UINT64_C(0x0001000000000000)
#define HM_CHANGED_SVM_MASK                      UINT64_C(0x0001000000000000)
#define HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE   HM_CHANGED_GUEST_DR_MASK

#define HM_CHANGED_ALL_GUEST                     (  HM_CHANGED_GUEST_MASK \
                                                  | HM_CHANGED_KEEPER_STATE_MASK)

/** Mask of what state might have changed when IEM raised an exception.
 *  This is a based on IEM_CPUMCTX_EXTRN_XCPT_MASK. */
#define HM_CHANGED_RAISED_XCPT_MASK              (  HM_CHANGED_GUEST_GPRS_MASK  \
                                                  | HM_CHANGED_GUEST_RIP        \
                                                  | HM_CHANGED_GUEST_RFLAGS     \
                                                  | HM_CHANGED_GUEST_SS         \
                                                  | HM_CHANGED_GUEST_CS         \
                                                  | HM_CHANGED_GUEST_CR0        \
                                                  | HM_CHANGED_GUEST_CR3        \
                                                  | HM_CHANGED_GUEST_CR4        \
                                                  | HM_CHANGED_GUEST_APIC_TPR   \
                                                  | HM_CHANGED_GUEST_EFER_MSR   \
                                                  | HM_CHANGED_GUEST_DR7        \
                                                  | HM_CHANGED_GUEST_CR2        \
                                                  | HM_CHANGED_GUEST_SREG_MASK  \
                                                  | HM_CHANGED_GUEST_TABLE_MASK)

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
/** Mask of what state might have changed when \#VMEXIT is emulated. */
# define HM_CHANGED_SVM_VMEXIT_MASK              (  HM_CHANGED_GUEST_RSP         \
                                                  | HM_CHANGED_GUEST_RAX         \
                                                  | HM_CHANGED_GUEST_RIP         \
                                                  | HM_CHANGED_GUEST_RFLAGS      \
                                                  | HM_CHANGED_GUEST_CS          \
                                                  | HM_CHANGED_GUEST_SS          \
                                                  | HM_CHANGED_GUEST_DS          \
                                                  | HM_CHANGED_GUEST_ES          \
                                                  | HM_CHANGED_GUEST_GDTR        \
                                                  | HM_CHANGED_GUEST_IDTR        \
                                                  | HM_CHANGED_GUEST_CR_MASK     \
                                                  | HM_CHANGED_GUEST_EFER_MSR    \
                                                  | HM_CHANGED_GUEST_DR6         \
                                                  | HM_CHANGED_GUEST_DR7         \
                                                  | HM_CHANGED_GUEST_OTHER_MSRS  \
                                                  | HM_CHANGED_GUEST_HWVIRT      \
                                                  | HM_CHANGED_SVM_MASK          \
                                                  | HM_CHANGED_GUEST_APIC_TPR)

/** Mask of what state might have changed when VMRUN is emulated. */
# define HM_CHANGED_SVM_VMRUN_MASK               HM_CHANGED_SVM_VMEXIT_MASK
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/** Mask of what state might have changed when VM-exit is emulated.
 *
 *  This is currently unused, but keeping it here in case we can get away a bit more
 *  fine-grained state handling.
 *
 *  @note Update IEM_CPUMCTX_EXTRN_VMX_VMEXIT_MASK when this changes. */
# define HM_CHANGED_VMX_VMEXIT_MASK             (  HM_CHANGED_GUEST_CR0 | HM_CHANGED_GUEST_CR3 | HM_CHANGED_GUEST_CR4 \
                                                 | HM_CHANGED_GUEST_DR7 | HM_CHANGED_GUEST_DR6 \
                                                 | HM_CHANGED_GUEST_EFER_MSR \
                                                 | HM_CHANGED_GUEST_SYSENTER_MSR_MASK \
                                                 | HM_CHANGED_GUEST_OTHER_MSRS    /* for PAT MSR */ \
                                                 | HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RSP | HM_CHANGED_GUEST_RFLAGS \
                                                 | HM_CHANGED_GUEST_SREG_MASK \
                                                 | HM_CHANGED_GUEST_TR \
                                                 | HM_CHANGED_GUEST_LDTR | HM_CHANGED_GUEST_GDTR | HM_CHANGED_GUEST_IDTR \
                                                 | HM_CHANGED_GUEST_HWVIRT )
#endif
/** @} */

/** Maximum number of exit reason statistics counters. */
#define MAX_EXITREASON_STAT                        0x100
#define MASK_EXITREASON_STAT                       0xff
#define MASK_INJECT_IRQ_STAT                       0xff

/** Size for the EPT identity page table (1024 4 MB pages to cover the entire address space). */
#define HM_EPT_IDENTITY_PG_TABLE_SIZE               PAGE_SIZE
/** Size of the TSS structure + 2 pages for the IO bitmap + end byte. */
#define HM_VTX_TSS_SIZE                             (sizeof(VBOXTSS) + 2 * PAGE_SIZE + 1)
/** Total guest mapped memory needed. */
#define HM_VTX_TOTAL_DEVHEAP_MEM                    (HM_EPT_IDENTITY_PG_TABLE_SIZE + HM_VTX_TSS_SIZE)


/** @name Macros for enabling and disabling preemption.
 * These are really just for hiding the RTTHREADPREEMPTSTATE and asserting that
 * preemption has already been disabled when there is no context hook.
 * @{ */
#ifdef VBOX_STRICT
# define HM_DISABLE_PREEMPT(a_pVCpu) \
    RTTHREADPREEMPTSTATE PreemptStateInternal = RTTHREADPREEMPTSTATE_INITIALIZER; \
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD) || VMMR0ThreadCtxHookIsEnabled((a_pVCpu))); \
    RTThreadPreemptDisable(&PreemptStateInternal)
#else
# define HM_DISABLE_PREEMPT(a_pVCpu) \
    RTTHREADPREEMPTSTATE PreemptStateInternal = RTTHREADPREEMPTSTATE_INITIALIZER; \
    RTThreadPreemptDisable(&PreemptStateInternal)
#endif /* VBOX_STRICT */
#define HM_RESTORE_PREEMPT()    do { RTThreadPreemptRestore(&PreemptStateInternal); } while(0)
/** @} */


/** @name HM saved state versions.
 * @{
 */
#define HM_SAVED_STATE_VERSION                         HM_SAVED_STATE_VERSION_SVM_NESTED_HWVIRT
#define HM_SAVED_STATE_VERSION_SVM_NESTED_HWVIRT       6
#define HM_SAVED_STATE_VERSION_TPR_PATCHING            5
#define HM_SAVED_STATE_VERSION_NO_TPR_PATCHING         4
#define HM_SAVED_STATE_VERSION_2_0_X                   3
/** @} */


/**
 * HM physical (host) CPU information.
 */
typedef struct HMPHYSCPU
{
    /** The CPU ID. */
    RTCPUID             idCpu;
    /** The VM_HSAVE_AREA (AMD-V) / VMXON region (Intel) memory backing. */
    RTR0MEMOBJ          hMemObj;
    /** The physical address of the first page in hMemObj (it's a
     *  physcially contigous allocation if it spans multiple pages). */
    RTHCPHYS            HCPhysMemObj;
    /** The address of the memory (for pfnEnable). */
    void               *pvMemObj;
    /** Current ASID (AMD-V) / VPID (Intel). */
    uint32_t            uCurrentAsid;
    /** TLB flush count. */
    uint32_t            cTlbFlushes;
    /** Whether to flush each new ASID/VPID before use. */
    bool                fFlushAsidBeforeUse;
    /** Configured for VT-x or AMD-V. */
    bool                fConfigured;
    /** Set if the VBOX_HWVIRTEX_IGNORE_SVM_IN_USE hack is active. */
    bool                fIgnoreAMDVInUseError;
    /** Whether CR4.VMXE was already enabled prior to us enabling it. */
    bool                fVmxeAlreadyEnabled;
    /** In use by our code. (for power suspend) */
    bool volatile       fInUse;
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    /** Nested-guest union (put data common to SVM/VMX outside the union). */
    union
    {
        /** Nested-guest SVM data. */
        struct
        {
            /** The active nested-guest MSR permission bitmap memory backing. */
            RTR0MEMOBJ          hNstGstMsrpm;
            /** The physical address of the first page in hNstGstMsrpm (physcially
             *  contiguous allocation). */
            RTHCPHYS            HCPhysNstGstMsrpm;
            /** The address of the active nested-guest MSRPM. */
            void               *pvNstGstMsrpm;
        } svm;
        /** @todo Nested-VMX. */
    } n;
#endif
} HMPHYSCPU;
/** Pointer to HMPHYSCPU struct. */
typedef HMPHYSCPU *PHMPHYSCPU;
/** Pointer to a const HMPHYSCPU struct. */
typedef const HMPHYSCPU *PCHMPHYSCPU;

/**
 * TPR-instruction type.
 */
typedef enum
{
    HMTPRINSTR_INVALID,
    HMTPRINSTR_READ,
    HMTPRINSTR_READ_SHR4,
    HMTPRINSTR_WRITE_REG,
    HMTPRINSTR_WRITE_IMM,
    HMTPRINSTR_JUMP_REPLACEMENT,
    /** The usual 32-bit paranoia. */
    HMTPRINSTR_32BIT_HACK   = 0x7fffffff
} HMTPRINSTR;

/**
 * TPR patch information.
 */
typedef struct
{
    /** The key is the address of patched instruction. (32 bits GC ptr) */
    AVLOU32NODECORE         Core;
    /** Original opcode. */
    uint8_t                 aOpcode[16];
    /** Instruction size. */
    uint32_t                cbOp;
    /** Replacement opcode. */
    uint8_t                 aNewOpcode[16];
    /** Replacement instruction size. */
    uint32_t                cbNewOp;
    /** Instruction type. */
    HMTPRINSTR              enmType;
    /** Source operand. */
    uint32_t                uSrcOperand;
    /** Destination operand. */
    uint32_t                uDstOperand;
    /** Number of times the instruction caused a fault. */
    uint32_t                cFaults;
    /** Patch address of the jump replacement. */
    RTGCPTR32               pJumpTarget;
} HMTPRPATCH;
/** Pointer to HMTPRPATCH. */
typedef HMTPRPATCH *PHMTPRPATCH;
/** Pointer to a const HMTPRPATCH. */
typedef const HMTPRPATCH *PCHMTPRPATCH;


/**
 * Makes a HMEXITSTAT::uKey value from a program counter and an exit code.
 *
 * @returns 64-bit key
 * @param   a_uPC           The RIP + CS.BASE value of the exit.
 * @param   a_uExit         The exit code.
 * @todo    Add CPL?
 */
#define HMEXITSTAT_MAKE_KEY(a_uPC, a_uExit) (((a_uPC) & UINT64_C(0x0000ffffffffffff)) | (uint64_t)(a_uExit) << 48)

typedef struct HMEXITINFO
{
    /** See HMEXITSTAT_MAKE_KEY(). */
    uint64_t                uKey;
    /** Number of recent hits (depreciates with time). */
    uint32_t volatile       cHits;
    /** The age + lock. */
    uint16_t volatile       uAge;
    /** Action or action table index. */
    uint16_t                iAction;
} HMEXITINFO;
AssertCompileSize(HMEXITINFO, 16); /* Lots of these guys, so don't add any unnecessary stuff! */

typedef struct HMEXITHISTORY
{
    /** The exit timestamp. */
    uint64_t                uTscExit;
    /** The index of the corresponding HMEXITINFO entry.
     * UINT32_MAX if none (too many collisions, race, whatever). */
    uint32_t                iExitInfo;
    /** Figure out later, needed for padding now. */
    uint32_t                uSomeClueOrSomething;
} HMEXITHISTORY;

/**
 * Switcher function, HC to the special 64-bit RC.
 *
 * @param   pVM             The cross context VM structure.
 * @param   offCpumVCpu     Offset from pVM->cpum to pVM->aCpus[idCpu].cpum.
 * @returns Return code indicating the action to take.
 */
typedef DECLCALLBACKTYPE(int, FNHMSWITCHERHC,(PVM pVM, uint32_t offCpumVCpu));
/** Pointer to switcher function. */
typedef FNHMSWITCHERHC *PFNHMSWITCHERHC;


/**
 * HM event.
 *
 * VT-x and AMD-V common event injection structure.
 */
typedef struct HMEVENT
{
    /** Whether the event is pending. */
    uint32_t        fPending;
    /** The error-code associated with the event. */
    uint32_t        u32ErrCode;
    /** The length of the instruction in bytes (only relevant for software
     *  interrupts or software exceptions). */
    uint32_t        cbInstr;
    /** Alignment. */
    uint32_t        u32Padding;
    /** The encoded event (VM-entry interruption-information for VT-x or EVENTINJ
     *  for SVM). */
    uint64_t        u64IntInfo;
    /** Guest virtual address if this is a page-fault event. */
    RTGCUINTPTR     GCPtrFaultAddress;
} HMEVENT;
/** Pointer to a HMEVENT struct. */
typedef HMEVENT *PHMEVENT;
/** Pointer to a const HMEVENT struct. */
typedef const HMEVENT *PCHMEVENT;
AssertCompileSizeAlignment(HMEVENT, 8);

/**
 * HM VM Instance data.
 * Changes to this must checked against the padding of the hm union in VM!
 */
typedef struct HM
{
    /** Set when the debug facility has breakpoints/events enabled that requires
     *  us to use the debug execution loop in ring-0. */
    bool                        fUseDebugLoop;
    /** Set when TPR patching is allowed. */
    bool                        fTprPatchingAllowed;
    /** Set when TPR patching is active. */
    bool                        fTprPatchingActive;
    /** Alignment padding. */
    bool                        afAlignment1[5];

    struct
    {
        /** Set by the ring-0 side of HM to indicate VMX is supported by the CPU. */
        bool                        fSupported;
        /** Set when we've enabled VMX. */
        bool                        fEnabled;
        /** The shift mask employed by the VMX-Preemption timer (set by ring-0). */
        uint8_t                     cPreemptTimerShift;
        bool                        fAlignment1;

        /** @name Configuration (gets copied if problematic)
         * @{ */
        /** Set if Last Branch Record (LBR) is enabled. */
        bool                        fLbrCfg;
        /** Set if VT-x VPID is allowed. */
        bool                        fAllowVpid;
        /** Set if unrestricted guest execution is in use (real and protected mode
         *  without paging). */
        bool                        fUnrestrictedGuestCfg;
        /** Set if the preemption timer should be used if available.  Ring-0
         * quietly clears this if the hardware doesn't support the preemption timer. */
        bool                        fUsePreemptTimerCfg;
        /** @} */

        /** Pause-loop exiting (PLE) gap in ticks. */
        uint32_t                    cPleGapTicks;
        /** Pause-loop exiting (PLE) window in ticks. */
        uint32_t                    cPleWindowTicks;

        /** Virtual address of the TSS page used for real mode emulation. */
        R3PTRTYPE(PVBOXTSS)         pRealModeTSS;
        /** Virtual address of the identity page table used for real mode and protected
         *  mode without paging emulation in EPT mode. */
        R3PTRTYPE(PX86PD)           pNonPagingModeEPTPageTable;
    } vmx;

    struct
    {
        /** Set by the ring-0 side of HM to indicate SVM is supported by the CPU. */
        bool                        fSupported;
        /** Set when we've enabled SVM. */
        bool                        fEnabled;
        /** Set when the hack to ignore VERR_SVM_IN_USE is active.
         * @todo Safe?  */
        bool                        fIgnoreInUseError;
        /** Whether to use virtualized VMSAVE/VMLOAD feature. */
        bool                        fVirtVmsaveVmload;
        /** Whether to use virtual GIF feature. */
        bool                        fVGif;
        /** Whether to use LBR virtualization feature. */
        bool                        fLbrVirt;
        bool                        afAlignment1[2];

        /** Pause filter counter. */
        uint16_t                    cPauseFilter;
        /** Pause filter treshold in ticks. */
        uint16_t                    cPauseFilterThresholdTicks;
        uint32_t                    u32Alignment2;
    } svm;

    /** AVL tree with all patches (active or disabled) sorted by guest instruction address.
     * @todo For @bugref{9217} this AVL tree must be eliminated and instead
     *       sort aPatches by address and do a safe binary search on it. */
    AVLOU32TREE                 PatchTree;
    uint32_t                    cPatches;
    HMTPRPATCH                  aPatches[64];

    /** Guest allocated memory for patching purposes. */
    RTGCPTR                     pGuestPatchMem;
    /** Current free pointer inside the patch block. */
    RTGCPTR                     pFreeGuestPatchMem;
    /** Size of the guest patch memory block. */
    uint32_t                    cbGuestPatchMem;
    uint32_t                    u32Alignment2;

    /** For ring-3 use only. */
    struct
    {
        /** Last recorded error code during HM ring-0 init. */
        int32_t                     rcInit;
        uint32_t                    u32Alignment3;

        /** Maximum ASID allowed.
         * This is mainly for the release log.  */
        uint32_t                    uMaxAsid;
        /** World switcher flags (HM_WSF_XXX) for the release log. */
        uint32_t                    fWorldSwitcher;

        struct
        {
            /** Set if VPID is supported (ring-3 copy). */
            bool                        fVpid;
            /** Whether the CPU supports VMCS fields for swapping EFER (set by ring-0 VMX
             *  init, for logging). */
            bool                        fSupportsVmcsEfer;
            /** Whether to use VMCS shadowing. */
            bool                        fUseVmcsShadowing;
            bool                        fAlignment2;

            /** Host CR4 value (set by ring-0 VMX init, for logging). */
            uint64_t                    u64HostCr4;
            /** Host SMM monitor control (set by ring-0 VMX init, for logging). */
            uint64_t                    u64HostSmmMonitorCtl;
            /** Host EFER value (set by ring-0 VMX init, for logging and guest NX). */
            uint64_t                    u64HostMsrEfer;

            /** The first valid host LBR branch-from-IP stack range. */
            uint32_t                    idLbrFromIpMsrFirst;
            /** The last valid host LBR branch-from-IP stack range. */
            uint32_t                    idLbrFromIpMsrLast;

            /** The first valid host LBR branch-to-IP stack range. */
            uint32_t                    idLbrToIpMsrFirst;
            /** The last valid host LBR branch-to-IP stack range. */
            uint32_t                    idLbrToIpMsrLast;

            /** Host-physical address for a failing VMXON instruction (for diagnostics, ring-3). */
            RTHCPHYS                    HCPhysVmxEnableError;
            /** VMX MSR values (only for ring-3 consumption). */
            VMXMSRS                     Msrs;

            /** Tagged-TLB flush type (only for ring-3 consumption). */
            VMXTLBFLUSHTYPE             enmTlbFlushType;
            /** Flush type to use for INVEPT (only for ring-3 consumption). */
            VMXTLBFLUSHEPT              enmTlbFlushEpt;
            /** Flush type to use for INVVPID (only for ring-3 consumption). */
            VMXTLBFLUSHVPID             enmTlbFlushVpid;
        } vmx;

        struct
        {
            /** SVM revision. */
            uint32_t                    u32Rev;
            /** SVM feature bits from cpuid 0x8000000a, ring-3 copy. */
            uint32_t                    fFeatures;
            /** HWCR MSR (for diagnostics). */
            uint64_t                    u64MsrHwcr;
        } svm;
    } ForR3;

    /** @name Configuration not used (much) after VM setup
     * @{ */
    /** The maximum number of resumes loops allowed in ring-0 (safety precaution).
     * This number is set much higher when RTThreadPreemptIsPending is reliable. */
    uint32_t                    cMaxResumeLoopsCfg;
    /** Set if nested paging is enabled.
     * Config value that is copied to HMR0PERVM::fNestedPaging on setup. */
    bool                        fNestedPagingCfg;
    /** Set if large pages are enabled (requires nested paging).
     * Config only, passed on the PGM where it really belongs.
     * @todo move to PGM */
    bool                        fLargePages;
    /** Set if we can support 64-bit guests or not.
     * Config value that is copied to HMR0PERVM::fAllow64BitGuests on setup. */
    bool                        fAllow64BitGuestsCfg;
    /** Set when we initialize VT-x or AMD-V once for all CPUs. */
    bool                        fGlobalInit;
    /** Set if hardware APIC virtualization is enabled.
     * @todo Not really used by HM, move to APIC where it's actually used.  */
    bool                        fVirtApicRegs;
    /** Set if posted interrupt processing is enabled.
     * @todo Not really used by HM, move to APIC where it's actually used.  */
    bool                        fPostedIntrs;
    /** @} */

    /** @name Processed into HMR0PERVCPU::fWorldSwitcher by ring-0 on VM init.
     * @{ */
    /** Set if indirect branch prediction barrier on VM exit. */
    bool                        fIbpbOnVmExit;
    /** Set if indirect branch prediction barrier on VM entry. */
    bool                        fIbpbOnVmEntry;
    /** Set if level 1 data cache should be flushed on VM entry. */
    bool                        fL1dFlushOnVmEntry;
    /** Set if level 1 data cache should be flushed on EMT scheduling. */
    bool                        fL1dFlushOnSched;
    /** Set if MDS related buffers should be cleared on VM entry. */
    bool                        fMdsClearOnVmEntry;
    /** Set if MDS related buffers should be cleared on EMT scheduling. */
    bool                        fMdsClearOnSched;
    /** Set if host manages speculation control settings.
     * @todo doesn't do anything ...  */
    bool                        fSpecCtrlByHost;
    /** @} */

    /** Set when we've finalized the VMX / SVM initialization in ring-3
     * (hmR3InitFinalizeR0Intel / hmR3InitFinalizeR0Amd). */
    bool                        fInitialized;

    bool                        afAlignment2[6];

    STAMCOUNTER                 StatTprPatchSuccess;
    STAMCOUNTER                 StatTprPatchFailure;
    STAMCOUNTER                 StatTprReplaceSuccessCr8;
    STAMCOUNTER                 StatTprReplaceSuccessVmc;
    STAMCOUNTER                 StatTprReplaceFailure;
} HM;
/** Pointer to HM VM instance data. */
typedef HM *PHM;
AssertCompileMemberAlignment(HM, StatTprPatchSuccess, 8);
AssertCompileMemberAlignment(HM, vmx,                 8);
AssertCompileMemberAlignment(HM, svm,                 8);
AssertCompileMemberAlignment(HM, StatTprPatchSuccess, 8);
AssertCompile(RTASSERT_OFFSET_OF(HM, PatchTree) <= 64); /* First cache line has the essentials for both VT-x and SVM operation. */


/**
 * Per-VM ring-0 instance data for HM.
 */
typedef struct HMR0PERVM
{
    /** The maximum number of resumes loops allowed in ring-0 (safety precaution).
     * This number is set much higher when RTThreadPreemptIsPending is reliable. */
    uint32_t                    cMaxResumeLoops;

    /** Set if nested paging is enabled. */
    bool                        fNestedPaging;
    /** Set if we can support 64-bit guests or not. */
    bool                        fAllow64BitGuests;
    bool                        afAlignment1[1];

    /** AMD-V specific data. */
    struct HMR0SVMVM
    {
        /** Set if erratum 170 affects the AMD cpu. */
        bool                        fAlwaysFlushTLB;
    } svm;

    /** VT-x specific data. */
    struct HMR0VMXVM
    {
        /** Set if unrestricted guest execution is in use (real and protected mode
         *  without paging). */
        bool                        fUnrestrictedGuest;
        /** Set if the preemption timer is in use. */
        bool                        fUsePreemptTimer;
        /** Whether to use VMCS shadowing. */
        bool                        fUseVmcsShadowing;
        /** Set if Last Branch Record (LBR) is enabled. */
        bool                        fLbr;
        bool                        afAlignment2[3];

        /** Set if VPID is supported (copy in HM::vmx::fVpidForRing3). */
        bool                        fVpid;
        /** Tagged-TLB flush type. */
        VMXTLBFLUSHTYPE             enmTlbFlushType;
        /** Flush type to use for INVEPT. */
        VMXTLBFLUSHEPT              enmTlbFlushEpt;
        /** Flush type to use for INVVPID. */
        VMXTLBFLUSHVPID             enmTlbFlushVpid;

        /** The host LBR TOS (top-of-stack) MSR id. */
        uint32_t                    idLbrTosMsr;

        /** The first valid host LBR branch-from-IP stack range. */
        uint32_t                    idLbrFromIpMsrFirst;
        /** The last valid host LBR branch-from-IP stack range. */
        uint32_t                    idLbrFromIpMsrLast;

        /** The first valid host LBR branch-to-IP stack range. */
        uint32_t                    idLbrToIpMsrFirst;
        /** The last valid host LBR branch-to-IP stack range. */
        uint32_t                    idLbrToIpMsrLast;

        /** Pointer to the VMREAD bitmap. */
        R0PTRTYPE(void *)           pvVmreadBitmap;
        /** Pointer to the VMWRITE bitmap. */
        R0PTRTYPE(void *)           pvVmwriteBitmap;

        /** Pointer to the shadow VMCS read-only fields array. */
        R0PTRTYPE(uint32_t *)       paShadowVmcsRoFields;
        /** Pointer to the shadow VMCS read/write fields array. */
        R0PTRTYPE(uint32_t *)       paShadowVmcsFields;
        /** Number of elements in the shadow VMCS read-only fields array. */
        uint32_t                    cShadowVmcsRoFields;
        /** Number of elements in the shadow VMCS read-write fields array. */
        uint32_t                    cShadowVmcsFields;

        /** Host-physical address of the APIC-access page. */
        RTHCPHYS                    HCPhysApicAccess;
        /** Host-physical address of the VMREAD bitmap. */
        RTHCPHYS                    HCPhysVmreadBitmap;
        /** Host-physical address of the VMWRITE bitmap. */
        RTHCPHYS                    HCPhysVmwriteBitmap;

#ifdef VBOX_WITH_CRASHDUMP_MAGIC
        /** Host-physical address of the crash-dump scratch area. */
        RTHCPHYS                    HCPhysScratch;
        /** Pointer to the crash-dump scratch bitmap. */
        R0PTRTYPE(uint8_t *)        pbScratch;
#endif

        /** Ring-0 memory object for per-VM VMX structures. */
        RTR0MEMOBJ                  hMemObj;
        /** Virtual address of the APIC-access page (not used). */
        R0PTRTYPE(uint8_t *)        pbApicAccess;
    } vmx;
} HMR0PERVM;
/** Pointer to HM's per-VM ring-0 instance data. */
typedef HMR0PERVM *PHMR0PERVM;


/** @addtogroup grp_hm_int_svm  SVM Internal
 * @{ */
/** SVM VMRun function, see SVMR0VMRun(). */
typedef DECLCALLBACKTYPE(int, FNHMSVMVMRUN,(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhysVMCB));
/** Pointer to a SVM VMRun function. */
typedef R0PTRTYPE(FNHMSVMVMRUN *) PFNHMSVMVMRUN;

/**
 * SVM nested-guest VMCB cache.
 *
 * Contains VMCB fields from the nested-guest VMCB before they're modified by
 * SVM R0 code for hardware-assisted SVM execution of a nested-guest.
 *
 * A VMCB field needs to be cached when it needs to be modified for execution using
 * hardware-assisted SVM and any of the following are true:
 *   - If the original field needs to be inspected during execution of the
 *     nested-guest or \#VMEXIT processing.
 *   - If the field is written back to memory on \#VMEXIT by the physical CPU.
 *
 * A VMCB field needs to be restored only when the field is written back to
 * memory on \#VMEXIT by the physical CPU and thus would be visible to the
 * guest.
 *
 * @remarks Please update hmR3InfoSvmNstGstVmcbCache() when changes are made to
 *          this structure.
 */
typedef struct SVMNESTEDVMCBCACHE
{
    /** Cache of CRX read intercepts. */
    uint16_t            u16InterceptRdCRx;
    /** Cache of CRX write intercepts. */
    uint16_t            u16InterceptWrCRx;
    /** Cache of DRX read intercepts. */
    uint16_t            u16InterceptRdDRx;
    /** Cache of DRX write intercepts. */
    uint16_t            u16InterceptWrDRx;

    /** Cache of the pause-filter threshold. */
    uint16_t            u16PauseFilterThreshold;
    /** Cache of the pause-filter count. */
    uint16_t            u16PauseFilterCount;

    /** Cache of exception intercepts. */
    uint32_t            u32InterceptXcpt;
    /** Cache of control intercepts. */
    uint64_t            u64InterceptCtrl;

    /** Cache of the TSC offset. */
    uint64_t            u64TSCOffset;

    /** Cache of V_INTR_MASKING bit. */
    bool                fVIntrMasking;
    /** Cache of the nested-paging bit. */
    bool                fNestedPaging;
    /** Cache of the LBR virtualization bit. */
    bool                fLbrVirt;
    /** Whether the VMCB is cached by HM.  */
    bool                fCacheValid;
    /** Alignment. */
    bool                afPadding0[4];
} SVMNESTEDVMCBCACHE;
/** Pointer to the SVMNESTEDVMCBCACHE structure. */
typedef SVMNESTEDVMCBCACHE *PSVMNESTEDVMCBCACHE;
/** Pointer to a const SVMNESTEDVMCBCACHE structure. */
typedef const SVMNESTEDVMCBCACHE *PCSVMNESTEDVMCBCACHE;
AssertCompileSizeAlignment(SVMNESTEDVMCBCACHE, 8);

/** @} */


/** @addtogroup grp_hm_int_vmx  VMX Internal
 * @{ */
/**
 * VMX VMCS information, shared.
 *
 * This structure provides information maintained for and during the executing of a
 * guest (or nested-guest) VMCS (VM control structure) using hardware-assisted VMX.
 *
 * Note! The members here are ordered and aligned based on estimated frequency of
 * usage and grouped to fit within a cache line in hot code paths. Even subtle
 * changes here have a noticeable effect in the bootsector benchmarks. Modify with
 * care.
 */
typedef struct VMXVMCSINFOSHARED
{
    /** @name Real-mode emulation state.
     * @{ */
    /** Set if guest was executing in real mode (extra checks). */
    bool                        fWasInRealMode;
    /** Padding. */
    bool                        afPadding0[7];
    struct
    {
        X86DESCATTR             AttrCS;
        X86DESCATTR             AttrDS;
        X86DESCATTR             AttrES;
        X86DESCATTR             AttrFS;
        X86DESCATTR             AttrGS;
        X86DESCATTR             AttrSS;
        X86EFLAGS               Eflags;
        bool                    fRealOnV86Active;
        bool                    afPadding1[3];
    } RealMode;
    /** @} */

    /** @name LBR MSR data.
     *  @{ */
    /** List of LastBranch-From-IP MSRs. */
    uint64_t                    au64LbrFromIpMsr[32];
    /** List of LastBranch-To-IP MSRs. */
    uint64_t                    au64LbrToIpMsr[32];
    /** The MSR containing the index to the most recent branch record.  */
    uint64_t                    u64LbrTosMsr;
    /** @} */
} VMXVMCSINFOSHARED;
/** Pointer to a VMXVMCSINFOSHARED struct.  */
typedef VMXVMCSINFOSHARED *PVMXVMCSINFOSHARED;
/** Pointer to a const VMXVMCSINFOSHARED struct. */
typedef const VMXVMCSINFOSHARED *PCVMXVMCSINFOSHARED;
AssertCompileSizeAlignment(VMXVMCSINFOSHARED, 8);


/**
 * VMX VMCS information, ring-0 only.
 *
 * This structure provides information maintained for and during the executing of a
 * guest (or nested-guest) VMCS (VM control structure) using hardware-assisted VMX.
 *
 * Note! The members here are ordered and aligned based on estimated frequency of
 * usage and grouped to fit within a cache line in hot code paths. Even subtle
 * changes here have a noticeable effect in the bootsector benchmarks. Modify with
 * care.
 */
typedef struct VMXVMCSINFO
{
    /** Pointer to the bits we share with ring-3. */
    R0PTRTYPE(PVMXVMCSINFOSHARED) pShared;

    /** @name Auxiliary information.
     * @{ */
    /** Host-physical address of the EPTP. */
    RTHCPHYS                    HCPhysEPTP;
    /** The VMCS launch state, see VMX_V_VMCS_LAUNCH_STATE_XXX. */
    uint32_t                    fVmcsState;
    /** The VMCS launch state of the shadow VMCS, see VMX_V_VMCS_LAUNCH_STATE_XXX. */
    uint32_t                    fShadowVmcsState;
    /** The host CPU for which its state has been exported to this VMCS. */
    RTCPUID                     idHostCpuState;
    /** The host CPU on which we last executed this VMCS. */
    RTCPUID                     idHostCpuExec;
    /** Number of guest MSRs in the VM-entry MSR-load area. */
    uint32_t                    cEntryMsrLoad;
    /** Number of guest MSRs in the VM-exit MSR-store area. */
    uint32_t                    cExitMsrStore;
    /** Number of host MSRs in the VM-exit MSR-load area. */
    uint32_t                    cExitMsrLoad;
    /** @} */

    /** @name Cache of execution related VMCS fields.
     *  @{ */
    /** Pin-based VM-execution controls. */
    uint32_t                    u32PinCtls;
    /** Processor-based VM-execution controls. */
    uint32_t                    u32ProcCtls;
    /** Secondary processor-based VM-execution controls. */
    uint32_t                    u32ProcCtls2;
    /** VM-entry controls. */
    uint32_t                    u32EntryCtls;
    /** VM-exit controls. */
    uint32_t                    u32ExitCtls;
    /** Exception bitmap. */
    uint32_t                    u32XcptBitmap;
    /** Page-fault exception error-code mask. */
    uint32_t                    u32XcptPFMask;
    /** Page-fault exception error-code match. */
    uint32_t                    u32XcptPFMatch;
    /** Padding. */
    uint32_t                    u32Alignment0;
    /** TSC offset. */
    uint64_t                    u64TscOffset;
    /** VMCS link pointer. */
    uint64_t                    u64VmcsLinkPtr;
    /** CR0 guest/host mask. */
    uint64_t                    u64Cr0Mask;
    /** CR4 guest/host mask. */
    uint64_t                    u64Cr4Mask;
    /** Current VMX_VMCS_HOST_RIP value (only used in HMR0A.asm). */
    uint64_t                    uHostRip;
    /** Current VMX_VMCS_HOST_RSP value (only used in HMR0A.asm). */
    uint64_t                    uHostRsp;
    /** @} */

    /** @name Host-virtual address of VMCS and related data structures.
     *  @{ */
    /** The VMCS. */
    R0PTRTYPE(void *)           pvVmcs;
    /** The shadow VMCS. */
    R0PTRTYPE(void *)           pvShadowVmcs;
    /** The virtual-APIC page. */
    R0PTRTYPE(uint8_t *)        pbVirtApic;
    /** The MSR bitmap. */
    R0PTRTYPE(void *)           pvMsrBitmap;
    /** The VM-entry MSR-load area. */
    R0PTRTYPE(void *)           pvGuestMsrLoad;
    /** The VM-exit MSR-store area. */
    R0PTRTYPE(void *)           pvGuestMsrStore;
    /** The VM-exit MSR-load area. */
    R0PTRTYPE(void *)           pvHostMsrLoad;
    /** @} */

    /** @name Host-physical address of VMCS and related data structures.
     *  @{ */
    /** The VMCS. */
    RTHCPHYS                    HCPhysVmcs;
    /** The shadow VMCS. */
    RTHCPHYS                    HCPhysShadowVmcs;
    /** The virtual APIC page. */
    RTHCPHYS                    HCPhysVirtApic;
    /** The MSR bitmap. */
    RTHCPHYS                    HCPhysMsrBitmap;
    /** The VM-entry MSR-load area. */
    RTHCPHYS                    HCPhysGuestMsrLoad;
    /** The VM-exit MSR-store area. */
    RTHCPHYS                    HCPhysGuestMsrStore;
    /** The VM-exit MSR-load area. */
    RTHCPHYS                    HCPhysHostMsrLoad;
    /** @} */

    /** @name R0-memory objects address for VMCS and related data structures.
     *  @{ */
    /** R0-memory object for VMCS and related data structures. */
    RTR0MEMOBJ                  hMemObj;
    /** @} */
} VMXVMCSINFO;
/** Pointer to a VMXVMCSINFOR0 struct.  */
typedef VMXVMCSINFO *PVMXVMCSINFO;
/** Pointer to a const VMXVMCSINFO struct. */
typedef const VMXVMCSINFO *PCVMXVMCSINFO;
AssertCompileSizeAlignment(VMXVMCSINFO, 8);
AssertCompileMemberAlignment(VMXVMCSINFO, u32PinCtls,      4);
AssertCompileMemberAlignment(VMXVMCSINFO, u64VmcsLinkPtr,  8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvVmcs,          8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvShadowVmcs,    8);
AssertCompileMemberAlignment(VMXVMCSINFO, pbVirtApic,      8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvMsrBitmap,     8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvGuestMsrLoad,  8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvGuestMsrStore, 8);
AssertCompileMemberAlignment(VMXVMCSINFO, pvHostMsrLoad,   8);
AssertCompileMemberAlignment(VMXVMCSINFO, HCPhysVmcs,      8);
AssertCompileMemberAlignment(VMXVMCSINFO, hMemObj,         8);


/** @name Host-state restoration flags.
 * @note If you change these values don't forget to update the assembly
 *       defines as well!
 * @{
 */
#define VMX_RESTORE_HOST_SEL_DS                                 RT_BIT(0)
#define VMX_RESTORE_HOST_SEL_ES                                 RT_BIT(1)
#define VMX_RESTORE_HOST_SEL_FS                                 RT_BIT(2)
#define VMX_RESTORE_HOST_SEL_GS                                 RT_BIT(3)
#define VMX_RESTORE_HOST_SEL_TR                                 RT_BIT(4)
#define VMX_RESTORE_HOST_GDTR                                   RT_BIT(5)
#define VMX_RESTORE_HOST_IDTR                                   RT_BIT(6)
#define VMX_RESTORE_HOST_GDT_READ_ONLY                          RT_BIT(7)
#define VMX_RESTORE_HOST_GDT_NEED_WRITABLE                      RT_BIT(8)
#define VMX_RESTORE_HOST_CAN_USE_WRFSBASE_AND_WRGSBASE          RT_BIT(9)
/**
 * This _must_ be the top most bit, so that we can easily that that it and
 * something else is set w/o having to do two checks like this:
 * @code
 *     if (   (pVCpu->hm.s.vmx.fRestoreHostFlags & VMX_RESTORE_HOST_REQUIRED)
 *         && (pVCpu->hm.s.vmx.fRestoreHostFlags & ~VMX_RESTORE_HOST_REQUIRED))
 * @endcode
 * Instead we can then do:
 * @code
 *     if (pVCpu->hm.s.vmx.fRestoreHostFlags > VMX_RESTORE_HOST_REQUIRED)
 * @endcode
 */
#define VMX_RESTORE_HOST_REQUIRED                               RT_BIT(10)
/** @} */

/**
 * Host-state restoration structure.
 *
 * This holds host-state fields that require manual restoration.
 * Assembly version found in HMInternal.mac (should be automatically verified).
 */
typedef struct VMXRESTOREHOST
{
    RTSEL       uHostSelDS;     /**< 0x00 */
    RTSEL       uHostSelES;     /**< 0x02 */
    RTSEL       uHostSelFS;     /**< 0x04 */
    X86XDTR64   HostGdtr;       /**< 0x06 - should be aligned by its 64-bit member. */
    RTSEL       uHostSelGS;     /**< 0x10 */
    RTSEL       uHostSelTR;     /**< 0x12 */
    RTSEL       uHostSelSS;     /**< 0x14 - not restored, just for fetching */
    X86XDTR64   HostGdtrRw;     /**< 0x16 - should be aligned by its 64-bit member. */
    RTSEL       uHostSelCS;     /**< 0x20 - not restored, just for fetching */
    uint8_t     abPadding1[4];  /**< 0x22 */
    X86XDTR64   HostIdtr;       /**< 0x26 - should be aligned by its 64-bit member. */
    uint64_t    uHostFSBase;    /**< 0x30 */
    uint64_t    uHostGSBase;    /**< 0x38 */
} VMXRESTOREHOST;
/** Pointer to VMXRESTOREHOST. */
typedef VMXRESTOREHOST *PVMXRESTOREHOST;
AssertCompileSize(X86XDTR64, 10);
AssertCompileMemberOffset(VMXRESTOREHOST, HostGdtr.uAddr,   0x08);
AssertCompileMemberOffset(VMXRESTOREHOST, HostGdtrRw.uAddr, 0x18);
AssertCompileMemberOffset(VMXRESTOREHOST, HostIdtr.uAddr,   0x28);
AssertCompileMemberOffset(VMXRESTOREHOST, uHostFSBase,      0x30);
AssertCompileSize(VMXRESTOREHOST, 64);
AssertCompileSizeAlignment(VMXRESTOREHOST, 8);

/**
 * VMX StartVM function.
 *
 * @returns VBox status code (no informational stuff).
 * @param   pVmcsInfo   Pointer to the VMCS info (for cached host RIP and RSP).
 * @param   pVCpu       Pointer to the cross context per-CPU structure.
 * @param   fResume     Whether to use VMRESUME (true) or VMLAUNCH (false).
 */
typedef DECLCALLBACKTYPE(int, FNHMVMXSTARTVM,(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume));
/** Pointer to a VMX StartVM function. */
typedef R0PTRTYPE(FNHMVMXSTARTVM *) PFNHMVMXSTARTVM;
/** @} */

/**
 * HM VMCPU Instance data.
 *
 * Note! If you change members of this struct, make sure to check if the
 * assembly counterpart in HMInternal.mac needs to be updated as well.
 *
 * Note! The members here are ordered and aligned based on estimated frequency of
 * usage and grouped to fit within a cache line in hot code paths. Even subtle
 * changes here have a noticeable effect in the bootsector benchmarks. Modify with
 * care.
 */
typedef struct HMCPU
{
    /** Set when the TLB has been checked until we return from the world switch. */
    bool volatile               fCheckedTLBFlush;
    /** Set when we're using VT-x or AMD-V at that moment.
     * @todo r=bird: Misleading description.  For AMD-V this will be set the first
     *       time HMCanExecuteGuest() is called and only cleared again by
     *       HMR3ResetCpu().  For VT-x it will be set by HMCanExecuteGuest when we
     *       can execute something in VT-x mode, and cleared if we cannot.
     *
     *       The field is much more about recording the last HMCanExecuteGuest
     *       return value than anything about any "moment". */
    bool                        fActive;

    /** Whether we should use the debug loop because of single stepping or special
     *  debug breakpoints / events are armed. */
    bool                        fUseDebugLoop;

    /** Whether \#UD needs to be intercepted (required by certain GIM providers). */
    bool                        fGIMTrapXcptUD;
    /** Whether \#GP needs to be intercepted for mesa driver workaround. */
    bool                        fTrapXcptGpForLovelyMesaDrv;
    /** Whether we're executing a single instruction. */
    bool                        fSingleInstruction;

    bool                        afAlignment0[2];

    /** An additional error code used for some gurus. */
    uint32_t                    u32HMError;
    /** The last exit-to-ring-3 reason. */
    int32_t                     rcLastExitToR3;
    /** CPU-context changed flags (see HM_CHANGED_xxx). */
    uint64_t                    fCtxChanged;

    /** VT-x data.   */
    struct HMCPUVMX
    {
        /** @name Guest information.
         * @{ */
        /** Guest VMCS information shared with ring-3. */
        VMXVMCSINFOSHARED           VmcsInfo;
        /** Nested-guest VMCS information shared with ring-3. */
        VMXVMCSINFOSHARED           VmcsInfoNstGst;
        /** Whether the nested-guest VMCS was the last current VMCS (shadow copy for ring-3).
         * @see HMR0PERVCPU::vmx.fSwitchedToNstGstVmcs  */
        bool                        fSwitchedToNstGstVmcsCopyForRing3;
        /** Whether the static guest VMCS controls has been merged with the
         *  nested-guest VMCS controls. */
        bool                        fMergedNstGstCtls;
        /** Whether the nested-guest VMCS has been copied to the shadow VMCS. */
        bool                        fCopiedNstGstToShadowVmcs;
        /** Whether flushing the TLB is required due to switching to/from the
         *  nested-guest. */
        bool                        fSwitchedNstGstFlushTlb;
        /** Alignment. */
        bool                        afAlignment0[4];
        /** Cached guest APIC-base MSR for identifying when to map the APIC-access page. */
        uint64_t                    u64GstMsrApicBase;
        /** @} */

        /** @name Error reporting and diagnostics.
         * @{ */
        /** VT-x error-reporting (mainly for ring-3 propagation). */
        struct
        {
            RTCPUID                 idCurrentCpu;
            RTCPUID                 idEnteredCpu;
            RTHCPHYS                HCPhysCurrentVmcs;
            uint32_t                u32VmcsRev;
            uint32_t                u32InstrError;
            uint32_t                u32ExitReason;
            uint32_t                u32GuestIntrState;
        } LastError;
        /** @} */
    } vmx;

    /** SVM data. */
    struct HMCPUSVM
    {
        /** Whether to emulate long mode support for sysenter/sysexit like intel CPUs
         *  does.   This means intercepting \#UD to emulate the instructions in
         *  long-mode and to intercept reads and writes to the SYSENTER MSRs in order to
         *  preserve the upper 32 bits written to them (AMD will ignore and discard). */
        bool                        fEmulateLongModeSysEnterExit;
        uint8_t                     au8Alignment0[7];

        /** Cache of the nested-guest's VMCB fields that we modify in order to run the
         *  nested-guest using AMD-V. This will be restored on \#VMEXIT. */
        SVMNESTEDVMCBCACHE          NstGstVmcbCache;
    } svm;

    /** Event injection state. */
    HMEVENT                 Event;

    /** Current shadow paging mode for updating CR4.
     * @todo move later (@bugref{9217}).  */
    PGMMODE                 enmShadowMode;
    uint32_t                u32TemporaryPadding;

    /** The PAE PDPEs used with Nested Paging (only valid when
     *  VMCPU_FF_HM_UPDATE_PAE_PDPES is set). */
    X86PDPE                 aPdpes[4];

    /* These two comes because they are accessed from assembly and we don't
       want to detail all the stats in the assembly version of this structure. */
    STAMCOUNTER             StatVmxWriteHostRip;
    STAMCOUNTER             StatVmxWriteHostRsp;
    STAMCOUNTER             StatVmxVmLaunch;
    STAMCOUNTER             StatVmxVmResume;

    STAMPROFILEADV          StatEntry;
    STAMPROFILEADV          StatPreExit;
    STAMPROFILEADV          StatExitHandling;
    STAMPROFILEADV          StatExitIO;
    STAMPROFILEADV          StatExitMovCRx;
    STAMPROFILEADV          StatExitXcptNmi;
    STAMPROFILEADV          StatExitVmentry;
    STAMPROFILEADV          StatImportGuestState;
    STAMPROFILEADV          StatExportGuestState;
    STAMPROFILEADV          StatLoadGuestFpuState;
    STAMPROFILEADV          StatInGC;
    STAMPROFILEADV          StatPoke;
    STAMPROFILEADV          StatSpinPoke;
    STAMPROFILEADV          StatSpinPokeFailed;

    STAMCOUNTER             StatInjectInterrupt;
    STAMCOUNTER             StatInjectXcpt;
    STAMCOUNTER             StatInjectReflect;
    STAMCOUNTER             StatInjectConvertDF;
    STAMCOUNTER             StatInjectInterpret;
    STAMCOUNTER             StatInjectReflectNPF;

    STAMCOUNTER             StatExitAll;
    STAMCOUNTER             StatNestedExitAll;
    STAMCOUNTER             StatExitShadowNM;
    STAMCOUNTER             StatExitGuestNM;
    STAMCOUNTER             StatExitShadowPF;       /**< Misleading, currently used for MMIO \#PFs as well. */
    STAMCOUNTER             StatExitShadowPFEM;
    STAMCOUNTER             StatExitGuestPF;
    STAMCOUNTER             StatExitGuestUD;
    STAMCOUNTER             StatExitGuestSS;
    STAMCOUNTER             StatExitGuestNP;
    STAMCOUNTER             StatExitGuestTS;
    STAMCOUNTER             StatExitGuestOF;
    STAMCOUNTER             StatExitGuestGP;
    STAMCOUNTER             StatExitGuestDE;
    STAMCOUNTER             StatExitGuestDF;
    STAMCOUNTER             StatExitGuestBR;
    STAMCOUNTER             StatExitGuestAC;
    STAMCOUNTER             StatExitGuestACSplitLock;
    STAMCOUNTER             StatExitGuestDB;
    STAMCOUNTER             StatExitGuestMF;
    STAMCOUNTER             StatExitGuestBP;
    STAMCOUNTER             StatExitGuestXF;
    STAMCOUNTER             StatExitGuestXcpUnk;
    STAMCOUNTER             StatExitDRxWrite;
    STAMCOUNTER             StatExitDRxRead;
    STAMCOUNTER             StatExitCR0Read;
    STAMCOUNTER             StatExitCR2Read;
    STAMCOUNTER             StatExitCR3Read;
    STAMCOUNTER             StatExitCR4Read;
    STAMCOUNTER             StatExitCR8Read;
    STAMCOUNTER             StatExitCR0Write;
    STAMCOUNTER             StatExitCR2Write;
    STAMCOUNTER             StatExitCR3Write;
    STAMCOUNTER             StatExitCR4Write;
    STAMCOUNTER             StatExitCR8Write;
    STAMCOUNTER             StatExitRdmsr;
    STAMCOUNTER             StatExitWrmsr;
    STAMCOUNTER             StatExitClts;
    STAMCOUNTER             StatExitXdtrAccess;
    STAMCOUNTER             StatExitLmsw;
    STAMCOUNTER             StatExitIOWrite;
    STAMCOUNTER             StatExitIORead;
    STAMCOUNTER             StatExitIOStringWrite;
    STAMCOUNTER             StatExitIOStringRead;
    STAMCOUNTER             StatExitIntWindow;
    STAMCOUNTER             StatExitExtInt;
    STAMCOUNTER             StatExitHostNmiInGC;
    STAMCOUNTER             StatExitHostNmiInGCIpi;
    STAMCOUNTER             StatExitPreemptTimer;
    STAMCOUNTER             StatExitTprBelowThreshold;
    STAMCOUNTER             StatExitTaskSwitch;
    STAMCOUNTER             StatExitApicAccess;
    STAMCOUNTER             StatExitReasonNpf;

    STAMCOUNTER             StatNestedExitReasonNpf;

    STAMCOUNTER             StatFlushPage;
    STAMCOUNTER             StatFlushPageManual;
    STAMCOUNTER             StatFlushPhysPageManual;
    STAMCOUNTER             StatFlushTlb;
    STAMCOUNTER             StatFlushTlbNstGst;
    STAMCOUNTER             StatFlushTlbManual;
    STAMCOUNTER             StatFlushTlbWorldSwitch;
    STAMCOUNTER             StatNoFlushTlbWorldSwitch;
    STAMCOUNTER             StatFlushEntire;
    STAMCOUNTER             StatFlushAsid;
    STAMCOUNTER             StatFlushNestedPaging;
    STAMCOUNTER             StatFlushTlbInvlpgVirt;
    STAMCOUNTER             StatFlushTlbInvlpgPhys;
    STAMCOUNTER             StatTlbShootdown;
    STAMCOUNTER             StatTlbShootdownFlush;

    STAMCOUNTER             StatSwitchPendingHostIrq;
    STAMCOUNTER             StatSwitchTprMaskedIrq;
    STAMCOUNTER             StatSwitchGuestIrq;
    STAMCOUNTER             StatSwitchHmToR3FF;
    STAMCOUNTER             StatSwitchVmReq;
    STAMCOUNTER             StatSwitchPgmPoolFlush;
    STAMCOUNTER             StatSwitchDma;
    STAMCOUNTER             StatSwitchExitToR3;
    STAMCOUNTER             StatSwitchLongJmpToR3;
    STAMCOUNTER             StatSwitchMaxResumeLoops;
    STAMCOUNTER             StatSwitchHltToR3;
    STAMCOUNTER             StatSwitchApicAccessToR3;
    STAMCOUNTER             StatSwitchPreempt;
    STAMCOUNTER             StatSwitchNstGstVmexit;

    STAMCOUNTER             StatTscParavirt;
    STAMCOUNTER             StatTscOffset;
    STAMCOUNTER             StatTscIntercept;

    STAMCOUNTER             StatDRxArmed;
    STAMCOUNTER             StatDRxContextSwitch;
    STAMCOUNTER             StatDRxIoCheck;

    STAMCOUNTER             StatExportMinimal;
    STAMCOUNTER             StatExportFull;
    STAMCOUNTER             StatLoadGuestFpu;
    STAMCOUNTER             StatExportHostState;

    STAMCOUNTER             StatVmxCheckBadRmSelBase;
    STAMCOUNTER             StatVmxCheckBadRmSelLimit;
    STAMCOUNTER             StatVmxCheckBadRmSelAttr;
    STAMCOUNTER             StatVmxCheckBadV86SelBase;
    STAMCOUNTER             StatVmxCheckBadV86SelLimit;
    STAMCOUNTER             StatVmxCheckBadV86SelAttr;
    STAMCOUNTER             StatVmxCheckRmOk;
    STAMCOUNTER             StatVmxCheckBadSel;
    STAMCOUNTER             StatVmxCheckBadRpl;
    STAMCOUNTER             StatVmxCheckPmOk;

    STAMCOUNTER             StatVmxPreemptionRecalcingDeadline;
    STAMCOUNTER             StatVmxPreemptionRecalcingDeadlineExpired;
    STAMCOUNTER             StatVmxPreemptionReusingDeadline;
    STAMCOUNTER             StatVmxPreemptionReusingDeadlineExpired;

#ifdef VBOX_WITH_STATISTICS
    R3PTRTYPE(PSTAMCOUNTER) paStatExitReason;
    R0PTRTYPE(PSTAMCOUNTER) paStatExitReasonR0;
    R3PTRTYPE(PSTAMCOUNTER) paStatInjectedIrqs;
    R0PTRTYPE(PSTAMCOUNTER) paStatInjectedIrqsR0;
    R3PTRTYPE(PSTAMCOUNTER) paStatInjectedXcpts;
    R0PTRTYPE(PSTAMCOUNTER) paStatInjectedXcptsR0;
    R3PTRTYPE(PSTAMCOUNTER) paStatNestedExitReason;
    R0PTRTYPE(PSTAMCOUNTER) paStatNestedExitReasonR0;
#endif
#ifdef HM_PROFILE_EXIT_DISPATCH
    STAMPROFILEADV          StatExitDispatch;
#endif
} HMCPU;
/** Pointer to HM VMCPU instance data. */
typedef HMCPU *PHMCPU;
AssertCompileMemberAlignment(HMCPU, fCheckedTLBFlush,  4);
AssertCompileMemberAlignment(HMCPU, fCtxChanged,       8);
AssertCompileMemberAlignment(HMCPU, vmx, 8);
AssertCompileMemberAlignment(HMCPU, vmx.VmcsInfo,       8);
AssertCompileMemberAlignment(HMCPU, vmx.VmcsInfoNstGst, 8);
AssertCompileMemberAlignment(HMCPU, svm, 8);
AssertCompileMemberAlignment(HMCPU, Event, 8);


/**
 * HM per-VCpu ring-0 only instance data.
 */
typedef struct HMR0PERVCPU
{
    /** World switch exit counter. */
    uint32_t volatile           cWorldSwitchExits;
    /** TLB flush count. */
    uint32_t                    cTlbFlushes;
    /** The last CPU we were executing code on (NIL_RTCPUID for the first time). */
    RTCPUID                     idLastCpu;
    /** The CPU ID of the CPU currently owning the VMCS. Set in
     * HMR0Enter and cleared in HMR0Leave. */
    RTCPUID                     idEnteredCpu;
    /** Current ASID in use by the VM. */
    uint32_t                    uCurrentAsid;

    /** Set if we need to flush the TLB during the world switch. */
    bool                        fForceTLBFlush;
    /** Whether we've completed the inner HM leave function. */
    bool                        fLeaveDone;
    /** Whether we're using the hyper DR7 or guest DR7. */
    bool                        fUsingHyperDR7;
    /** Whether we are currently executing in the debug loop.
     *  Mainly for assertions. */
    bool                        fUsingDebugLoop;
    /** Set if we using the debug loop and wish to intercept RDTSC. */
    bool                        fDebugWantRdTscExit;
    /** Set if XCR0 needs to be saved/restored when entering/exiting guest code
     *  execution. */
    bool                        fLoadSaveGuestXcr0;
    /** Set if we need to clear the trap flag because of single stepping. */
    bool                        fClearTrapFlag;

    bool                        afPadding1[1];
    /** World switcher flags (HM_WSF_XXX - was CPUMCTX::fWorldSwitcher in 6.1). */
    uint32_t                    fWorldSwitcher;
    /** The raw host TSC value from the last VM exit (set by HMR0A.asm). */
    uint64_t                    uTscExit;

    /** VT-x data.   */
    struct HMR0CPUVMX
    {
        /** Ring-0 pointer to the hardware-assisted VMX execution function. */
        PFNHMVMXSTARTVM             pfnStartVm;
        /** Absolute TSC deadline. */
        uint64_t                    uTscDeadline;
        /** The deadline version number. */
        uint64_t                    uTscDeadlineVersion;

        /** @name Guest information.
         * @{ */
        /** Guest VMCS information. */
        VMXVMCSINFO                 VmcsInfo;
        /** Nested-guest VMCS information. */
        VMXVMCSINFO                 VmcsInfoNstGst;
        /* Whether the nested-guest VMCS was the last current VMCS (authoritative copy).
         * @see HMCPU::vmx.fSwitchedToNstGstVmcsCopyForRing3  */
        bool                        fSwitchedToNstGstVmcs;
        bool                        afAlignment0[7];
        /** @} */

        /** @name Host information.
         * @{ */
        /** Host LSTAR MSR to restore lazily while leaving VT-x. */
        uint64_t                    u64HostMsrLStar;
        /** Host STAR MSR to restore lazily while leaving VT-x. */
        uint64_t                    u64HostMsrStar;
        /** Host SF_MASK MSR to restore lazily while leaving VT-x. */
        uint64_t                    u64HostMsrSfMask;
        /** Host KernelGS-Base MSR to restore lazily while leaving VT-x. */
        uint64_t                    u64HostMsrKernelGsBase;
        /** The mask of lazy MSRs swap/restore state, see VMX_LAZY_MSRS_XXX. */
        uint32_t                    fLazyMsrs;
        /** Whether the host MSR values are up-to-date in the auto-load/store MSR area. */
        bool                        fUpdatedHostAutoMsrs;
        /** Alignment. */
        uint8_t                     au8Alignment0[3];
        /** Which host-state bits to restore before being preempted, see
         * VMX_RESTORE_HOST_XXX. */
        uint32_t                    fRestoreHostFlags;
        /** Alignment. */
        uint32_t                    u32Alignment0;
        /** The host-state restoration structure. */
        VMXRESTOREHOST              RestoreHost;
        /** @} */
    } vmx;

    /** SVM data. */
    struct HMR0CPUSVM
    {
        /** Ring 0 handlers for VT-x. */
        PFNHMSVMVMRUN               pfnVMRun;

        /** Physical address of the host VMCB which holds additional host-state. */
        RTHCPHYS                    HCPhysVmcbHost;
        /** R0 memory object for the host VMCB which holds additional host-state. */
        RTR0MEMOBJ                  hMemObjVmcbHost;

        /** Physical address of the guest VMCB. */
        RTHCPHYS                    HCPhysVmcb;
        /** R0 memory object for the guest VMCB. */
        RTR0MEMOBJ                  hMemObjVmcb;
        /** Pointer to the guest VMCB. */
        R0PTRTYPE(PSVMVMCB)         pVmcb;

        /** Physical address of the MSR bitmap (8 KB). */
        RTHCPHYS                    HCPhysMsrBitmap;
        /** R0 memory object for the MSR bitmap (8 KB). */
        RTR0MEMOBJ                  hMemObjMsrBitmap;
        /** Pointer to the MSR bitmap. */
        R0PTRTYPE(void *)           pvMsrBitmap;

        /** Whether VTPR with V_INTR_MASKING set is in effect, indicating
         *  we should check if the VTPR changed on every VM-exit. */
        bool                        fSyncVTpr;
        bool                        afAlignment[7];

        /** Host's TSC_AUX MSR (used when RDTSCP doesn't cause VM-exits). */
        uint64_t                    u64HostTscAux;

        /** For saving stack space, the disassembler state is allocated here
         * instead of on the stack. */
        DISCPUSTATE                 DisState;
    } svm;
} HMR0PERVCPU;
/** Pointer to HM ring-0 VMCPU instance data. */
typedef HMR0PERVCPU *PHMR0PERVCPU;
AssertCompileMemberAlignment(HMR0PERVCPU, cWorldSwitchExits, 4);
AssertCompileMemberAlignment(HMR0PERVCPU, fForceTLBFlush,    4);
AssertCompileMemberAlignment(HMR0PERVCPU, vmx.RestoreHost,   8);


/** @name HM_WSF_XXX - @bugref{9453}, @bugref{9087}
 *  @note If you change these values don't forget to update the assembly
 *       defines as well!
 * @{ */
/** Touch IA32_PRED_CMD.IBPB on VM exit.   */
#define HM_WSF_IBPB_EXIT            RT_BIT_32(0)
/** Touch IA32_PRED_CMD.IBPB on VM entry. */
#define HM_WSF_IBPB_ENTRY           RT_BIT_32(1)
/** Touch IA32_FLUSH_CMD.L1D on VM entry. */
#define HM_WSF_L1D_ENTRY            RT_BIT_32(2)
/** Flush MDS buffers on VM entry. */
#define HM_WSF_MDS_ENTRY            RT_BIT_32(3)

/** Touch IA32_FLUSH_CMD.L1D on VM scheduling. */
#define HM_WSF_L1D_SCHED            RT_BIT_32(16)
/** Flush MDS buffers on VM scheduling. */
#define HM_WSF_MDS_SCHED            RT_BIT_32(17)
/** @} */


#ifdef IN_RING0
extern bool             g_fHmVmxSupported;
extern uint32_t         g_fHmHostKernelFeatures;
extern uint32_t         g_uHmMaxAsid;
extern bool             g_fHmVmxUsePreemptTimer;
extern uint8_t          g_cHmVmxPreemptTimerShift;
extern bool             g_fHmVmxSupportsVmcsEfer;
extern uint64_t         g_uHmVmxHostCr4;
extern uint64_t         g_uHmVmxHostMsrEfer;
extern uint64_t         g_uHmVmxHostSmmMonitorCtl;
extern bool             g_fHmSvmSupported;
extern uint32_t         g_uHmSvmRev;
extern uint32_t         g_fHmSvmFeatures;

extern SUPHWVIRTMSRS    g_HmMsrs;


VMMR0_INT_DECL(PHMPHYSCPU)  hmR0GetCurrentCpu(void);
VMMR0_INT_DECL(int)         hmR0EnterCpu(PVMCPUCC pVCpu);

# ifdef VBOX_STRICT
#  define HM_DUMP_REG_FLAGS_GPRS      RT_BIT(0)
#  define HM_DUMP_REG_FLAGS_FPU       RT_BIT(1)
#  define HM_DUMP_REG_FLAGS_MSRS      RT_BIT(2)
#  define HM_DUMP_REG_FLAGS_ALL       (HM_DUMP_REG_FLAGS_GPRS | HM_DUMP_REG_FLAGS_FPU | HM_DUMP_REG_FLAGS_MSRS)

VMMR0_INT_DECL(void)        hmR0DumpRegs(PVMCPUCC pVCpu, uint32_t fFlags);
VMMR0_INT_DECL(void)        hmR0DumpDescriptor(PCX86DESCHC pDesc, RTSEL Sel, const char *pszMsg);
# endif

DECLASM(void)               hmR0MdsClear(void);
#endif /* IN_RING0 */


/** @addtogroup grp_hm_int_svm  SVM Internal
 * @{ */
VMM_INT_DECL(int)           hmEmulateSvmMovTpr(PVMCC pVM, PVMCPUCC pVCpu);

/**
 * Prepares for and executes VMRUN (64-bit register context).
 *
 * @returns VBox status code (no informational stuff).
 * @param   pVM             The cross context VM structure. (Not used.)
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   HCPhyspVMCB     Physical address of the VMCB.
 *
 * @remarks With spectre mitigations and the usual need for speed (/ micro
 *          optimizations), we have a bunch of variations of this code depending
 *          on a few precoditions.  In release builds, the code is entirely
 *          without conditionals.  Debug builds have a couple of assertions that
 *          shouldn't ever be triggered.
 *
 * @{
 */
DECLASM(int) hmR0SvmVmRun_SansXcr0_SansIbpbEntry_SansIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
DECLASM(int) hmR0SvmVmRun_WithXcr0_SansIbpbEntry_SansIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
DECLASM(int) hmR0SvmVmRun_SansXcr0_WithIbpbEntry_SansIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
DECLASM(int) hmR0SvmVmRun_WithXcr0_WithIbpbEntry_SansIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
DECLASM(int) hmR0SvmVmRun_SansXcr0_SansIbpbEntry_WithIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
DECLASM(int) hmR0SvmVmRun_WithXcr0_SansIbpbEntry_WithIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
DECLASM(int) hmR0SvmVmRun_SansXcr0_WithIbpbEntry_WithIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
DECLASM(int) hmR0SvmVmRun_WithXcr0_WithIbpbEntry_WithIbpbExit(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhyspVMCB);
/** @} */

/** @} */


/** @addtogroup grp_hm_int_vmx  VMX Internal
 * @{ */
VMM_INT_DECL(PVMXVMCSINFOSHARED) hmGetVmxActiveVmcsInfoShared(PVMCPUCC pVCpu);

/**
 * Used on platforms with poor inline assembly support to retrieve all the
 * info from the CPU and put it in the @a pRestoreHost structure.
 */
DECLASM(void)               hmR0VmxExportHostSegmentRegsAsmHlp(PVMXRESTOREHOST pRestoreHost, bool fHaveFsGsBase);

/**
 * Restores some host-state fields that need not be done on every VM-exit.
 *
 * @returns VBox status code.
 * @param   fRestoreHostFlags   Flags of which host registers needs to be
 *                              restored.
 * @param   pRestoreHost        Pointer to the host-restore structure.
 */
DECLASM(int)                VMXRestoreHostState(uint32_t fRestoreHostFlags, PVMXRESTOREHOST pRestoreHost);

/**
 * VMX StartVM functions.
 *
 * @returns VBox status code (no informational stuff).
 * @param   pVmcsInfo   Pointer to the VMCS info (for cached host RIP and RSP).
 * @param   pVCpu       Pointer to the cross context per-CPU structure of the
 *                      calling EMT.
 * @param   fResume     Whether to use VMRESUME (true) or VMLAUNCH (false).
 *
 * @remarks With spectre mitigations and the usual need for speed (/ micro
 *          optimizations), we have a bunch of variations of this code depending
 *          on a few precoditions.  In release builds, the code is entirely
 *          without conditionals.  Debug builds have a couple of assertions that
 *          shouldn't ever be triggered.
 *
 * @{
 */
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_SansIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_SansL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_WithL1dEntry_SansMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_SansL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_SansIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_SansXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
DECLASM(int) hmR0VmxStartVm_WithXcr0_WithIbpbEntry_WithL1dEntry_WithMdsEntry_WithIbpbExit(PVMXVMCSINFO pVmcsInfo, PVMCPUCC pVCpu, bool fResume);
/** @} */

/** @} */

/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_HMInternal_h */

