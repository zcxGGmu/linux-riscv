/* SPDX-License-Identifier: GPL-2.0
 *
 * aia_init.c - Basic AIA (IMSIC) initialization test framework for RISC-V KVM
 *
 * This file implements basic selftests for RISC-V KVM's Advanced Interrupt Architecture (AIA),
 * focusing on device creation, address attribute configuration, and controller initialization.
 *
 * This test mimics the ARM GICv3 selftest framework (e.g., vgic_init.c), adapted to RISC-V.
 */

 #include <linux/kernel.h>
 #include <sys/syscall.h>
 #include <asm/kvm.h>
 #include <asm/kvm_para.h>
 
 #include "test_util.h"
 #include "kvm_util.h"
 #include "processor.h"
 
 #define NR_VCPUS        4
 
 /* Helper macro to encode register access with vcpu and offset */
 #define REG_OFFSET(vcpu, offset)    (((uint64_t)(vcpu) << 32) | (offset))
 
 /* Represents a virtual AIA device, analogous to struct vm_gic */
 struct vm_aia {
     struct kvm_vm *vm;
     int aia_fd;
     uint32_t aia_dev_type;
 };
 
 /* Dummy guest code for basic sync testing */
 static void guest_code(void)
 {
     GUEST_SYNC(0);
     GUEST_SYNC(1);
     GUEST_SYNC(2);
     GUEST_DONE();
 }
 
 /* Helper to run a single vCPU and return error code if any */
 static int run_vcpu(struct kvm_vcpu *vcpu)
 {
     return __vcpu_run(vcpu) ? -errno : 0;
 }
 
 /* Create a VM with multiple vCPUs and attach AIA device */
 static struct vm_aia vm_aia_create_with_vcpus(uint32_t aia_dev_type, uint32_t nr_vcpus, struct kvm_vcpu *vcpus[])
 {
     struct vm_aia a;
     a.aia_dev_type = aia_dev_type;
     a.vm = vm_create_with_vcpus(nr_vcpus, guest_code, vcpus);
     a.aia_fd = kvm_create_device(a.vm, aia_dev_type);
     return a;
 }
 
 /* Create a bare VM without vCPUs and attach AIA device */
 static struct vm_aia vm_aia_create_barebones(uint32_t aia_dev_type)
 {
     struct vm_aia a;
     a.aia_dev_type = aia_dev_type;
     a.vm = vm_create_barebones();
     a.aia_fd = kvm_create_device(a.vm, aia_dev_type);
     return a;
 }
 
 /* Destroy AIA device and free VM resources */
 static void vm_aia_destroy(struct vm_aia *a)
 {
     close(a->aia_fd);
     kvm_vm_free(a->vm);
 }
 
 /* Encodes IMSIC region attribute: count, base address, flags, index */
 #define IMSIC_REGION_ATTR_ADDR(count, base, flags, index)  \
     (((uint64_t)(count) << 48) | ((base) & 0xFFFFFFFFFFFFULL) | ((uint64_t)(flags) << 32) | (index))
 
 /* Attribute structure for IMSIC region configuration */
 struct aia_region_attr {
     uint64_t attr;
     uint64_t size;
     uint64_t alignment;
 };
 
 /* Example distributor region for IMSIC */
 struct aia_region_attr aia_dist_region = {
     .attr = KVM_DEV_RISCV_AIA_GRP_ADDR,
     .size = 0x10000,
     .alignment = 0x10000,
 };
 
 /* Example IMSIC CPU interface region, using placeholder attr value */
 struct aia_region_attr aia_imsic_region = {
     .attr = 0, /* TODO: define KVM_DEV_RISCV_AIA_ADDR_TYPE_IMSIC_REGION */
     .size = NR_VCPUS * 0x20000,
     .alignment = 0x10000,
 };
 
 /*
  * Basic subtest: test setting invalid and valid IMSIC region attributes.
  * Covers alignment errors, out-of-bound addresses, and correct setup.
  */
 static void subtest_imsic_region(struct vm_aia *a)
 {
     int ret;
     uint64_t addr;
 
     /* Test unsupported misaligned distributor address */
     kvm_has_device_attr(a->aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR, aia_dist_region.attr);
 
     addr = aia_dist_region.alignment / 0x10;
     ret = __kvm_device_attr_set(a->aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR, aia_dist_region.attr, &addr);
     TEST_ASSERT(ret && errno == EINVAL, "AIA dist base not aligned");
 
     /* Test out-of-bound physical address */
     extern uint64_t max_phys_size;
     addr = max_phys_size;
     ret = __kvm_device_attr_set(a->aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR, aia_dist_region.attr, &addr);
     TEST_ASSERT(ret && errno == E2BIG, "dist address beyond IPA limit");
 
     /* Set valid distributor address */
     addr = 0;
     kvm_device_attr_set(a->aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR, aia_dist_region.attr, &addr);
 
     /* Set misaligned IMSIC region address */
     addr = IMSIC_REGION_ATTR_ADDR(NR_VCPUS, aia_imsic_region.alignment / 0x10, 0, 0);
     ret = __kvm_device_attr_set(a->aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR, aia_imsic_region.attr, &addr);
     TEST_ASSERT(ret && errno == EINVAL, "IMSIC region not aligned");
 
     /* Set valid IMSIC region address */
     addr = IMSIC_REGION_ATTR_ADDR(NR_VCPUS, 0x100000, 0, 0);
     kvm_device_attr_set(a->aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR, aia_imsic_region.attr, &addr);
 }
 
 /*
  * Test flow: Create all VCPUs first, then set AIA attributes.
  * VCPU execution is expected to fail if IMSIC region is invalid or overlapping.
  */
 static void test_vcpus_then_aia(uint32_t aia_dev_type)
 {
     struct kvm_vcpu *vcpus[NR_VCPUS];
     struct vm_aia a;
     int ret;
 
     a = vm_aia_create_with_vcpus(aia_dev_type, NR_VCPUS, vcpus);
     subtest_imsic_region(&a);
 
     ret = run_vcpu(vcpus[3]);
     TEST_ASSERT(ret == -EINVAL, "IMSIC region overlap detected on VCPU run");
 
     vm_aia_destroy(&a);
 }
 
 /*
  * Test flow: Create AIA device first, then add vCPUs.
  * Running a vCPU should also detect invalid IMSIC region config.
  */
 static void test_aia_then_vcpus(uint32_t aia_dev_type)
 {
     struct kvm_vcpu *vcpus[NR_VCPUS];
     struct vm_aia a;
     int i, ret;
 
     a = vm_aia_create_with_vcpus(aia_dev_type, 1, vcpus);
     subtest_imsic_region(&a);
 
     for (i = 1; i < NR_VCPUS; ++i)
         vcpus[i] = vm_vcpu_add(a.vm, i, guest_code);
 
     ret = run_vcpu(vcpus[3]);
     TEST_ASSERT(ret == -EINVAL, "IMSIC region overlap detected on VCPU run");
 
     vm_aia_destroy(&a);
 }
 
 /*
  * Test AIA controller initialization via KVM_DEV_RISCV_AIA_CTRL_INIT.
  * VCPU should run successfully after valid IMSIC setup and init.
  */
 static void test_aia_ctrl_init(uint32_t aia_dev_type)
 {
     struct kvm_vcpu *vcpus[NR_VCPUS];
     struct vm_aia a;
     int ret;
 
     a = vm_aia_create_with_vcpus(aia_dev_type, NR_VCPUS, vcpus);
 
     uint64_t addr = IMSIC_REGION_ATTR_ADDR(NR_VCPUS, 0x100000, 0, 0);
     kvm_device_attr_set(a.aia_fd, KVM_DEV_RISCV_AIA_GRP_ADDR, aia_imsic_region.attr, &addr);
 
     kvm_device_attr_set(a.aia_fd, KVM_DEV_RISCV_AIA_GRP_CTRL, KVM_DEV_RISCV_AIA_CTRL_INIT, NULL);
 
     ret = run_vcpu(vcpus[3]);
     TEST_ASSERT(!ret, "VCPU run failed after AIA initialization");
 
     vm_aia_destroy(&a);
 }
 
 /* Global max IPA size for test constraints */
 uint64_t max_phys_size;
 
 /*
  * Entry point: invoke each subtest sequentially.
  */
 int main(int argc, char *argv[])
 {
     max_phys_size = 0x100000000ULL; // 4GB IPA space
 
     test_vcpus_then_aia(KVM_DEV_TYPE_RISCV_AIA);
     test_aia_then_vcpus(KVM_DEV_TYPE_RISCV_AIA);
     test_aia_ctrl_init(KVM_DEV_TYPE_RISCV_AIA);
 
     return 0;
 }
 
 /*
 * TODO: RISC-V AIA Selftest Support
 *
 * This test is currently modeled after ARM GICv3 support.
 * To complete RISC-V AIA selftest coverage, the following areas need to be implemented:
 *
 * - Implement creation and configuration of KVM_DEV_TYPE_RISCV_AIA.
 * - Add support for KVM_DEV_RISCV_AIA_GRP_ADDR attribute group:
 *   - Validate alignment and address range checks.
 *   - Reject overlapping or duplicate interrupt file regions.
 *
 * - Implement MSI injection tests for AIA using KVM_SIGNAL_MSI.
 *   - Validate delivery to correct hart/guest interrupt files.
 *   - Optionally support irqfd and irq_line interfaces if applicable.
 *
 * - Simulate interrupt preemption and active/pending state tracking.
 *   - Add tests similar to ISPENDR/ISACTIVER functionality.
 *   - Verify priority and nested interrupt handling.
 *
 * - Implement stress testing for AIA MSI delivery:
 *   - Multi-threaded injection, high volume delivery measurement.
 *   - Interrupt routing tests similar to MAPD/MAPTI logic in ITS.
 *
 * - Replace any GIC-specific code with AIA equivalents.
 *   - Avoid GIC headers and constants (e.g., GICR_TYPER, IAR_SPURIOUS).
 *   - Use RISC-V AIA-specific init/setup routines.
 *
 * This serves as a functional gap list to match the coverage of vgic_irq.c,
 * vgic_init.c, and vgic_lpi_stress.c for the RISC-V AIA virtualization path.
 */