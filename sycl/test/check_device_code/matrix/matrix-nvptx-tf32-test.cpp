// REQUIRES: cuda

// RUN: %clangxx -Xclang -no-opaque-pointers -fsycl-device-only -fsycl-targets=nvptx64-nvidia-cuda -Xsycl-target-backend --cuda-gpu-arch=sm_80 -DSYCL_EXT_ONEAPI_MATRIX_VERSION=3 -S -Xclang -emit-llvm %s -o -| FileCheck %s
// RUN: %clangxx -Xclang -opaque-pointers -fsycl-device-only -fsycl-targets=nvptx64-nvidia-cuda -Xsycl-target-backend --cuda-gpu-arch=sm_80 -DSYCL_EXT_ONEAPI_MATRIX_VERSION=3 -S -Xclang -emit-llvm %s -o -| FileCheck %s --check-prefixes=CHECK-OPAQUE

// IMPORTANT: before updating sm version support beyond sm_90 read the following
// NOTE!

// NOTE: Technically the 'wrong' ptx instruction is called by
// joint_matrix_load/joint_matrix_store in this case: notice that the load and
// store instructions use shape m16n16k16, rather than the correct shape
// m16n16k8. The 'wrong' ptx instruction is used because it returns the correct
// SASS instructions for all existing sm versions supporting tf32: sm_80, sm_86,
// sm_87, sm_89, and sm_90. The reason for this ptx instruction redundancy is
// due to the ptx naming convention for the mnk shape triple; however we cannot
// in principle a priori know that future sm versions will behave in the same
// way and that this redundancy will continue as future architecture is
// released. This should be validated before supporting any sm versions beyond
// sm_90. The reason that we choose to use the m16n16k16 instruction is that it
// allows us to use a simpler portable interface across Intel and Nvidia
// backends.

#include <sycl/sycl.hpp>

using namespace sycl;
using namespace sycl::ext::oneapi::experimental::matrix;

// M, N, K define the sizes of dimensions of the three matrix types (a, b,
// accumulator) used per subgroup operation.
constexpr int M = 16; // number of rows of accumulator,
                      // number of cols of b.
constexpr int N = 16; // number of cols of accumulator,
                      // number of rows of a.
constexpr int K = 8;  // number of cols of a/number of rows of b.

// float is used in this test as the storage type for tf32
float A[M * K];
float B[K * N];
float C[M * N];
float D[M * N];

int main() {

  buffer<float, 1> bufA(A, range<1>(M * K)); // will be used as tf32
  buffer<float, 1> bufB(B, range<1>(K * N)); // will be used as tf32
  buffer<float, 1> bufC(C, range<1>(M * N));
  buffer<float, 1> bufD(D, range<1>(M * N));

  queue q;

  q.submit([&](handler &cgh) {
    auto accA = bufA.get_access<access::mode::read_write>(cgh);
    auto accB = bufB.get_access<access::mode::read_write>(cgh);
    auto accC = bufC.get_access<access::mode::read_write>(cgh);
    auto accD = bufD.get_access<access::mode::read_write>(cgh);

    cgh.parallel_for<class row_row>(
        nd_range<2>({1, 32}, {1, 32}),
        [=](nd_item<2> item) [[sycl::reqd_work_group_size(1, 1, 32)]] {
          sycl::sub_group sg = item.get_sub_group();

          joint_matrix<precision::tf32, matrix_use::a, M, K,
                       matrix_layout::row_major>
              sub_a;

          joint_matrix<precision::tf32, matrix_use::b, K, N,
                       matrix_layout::row_major>
              sub_b;

          joint_matrix<float, matrix_use::accumulator, M, N,
                       matrix_layout::row_major>
              sub_c;

          //CHECK: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.a.row.stride.tf32.p0i32(i32* %call.ascast.i.i{{.*}}.i, i32 8)
          //CHECK-OPAQUE: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.a.row.stride.tf32.p0(ptr %call.ascast.i.i{{.*}}.i, i32 8)
          joint_matrix_load(sg, sub_a, accA.get_pointer(), K);
          //CHECK: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.b.row.stride.tf32.p0i32(i32* %call.ascast.i.i{{.*}}.i, i32 16)
          //CHECK-OPAQUE: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.b.row.stride.tf32.p0(ptr %call.ascast.i.i{{.*}}.i, i32 16)
          joint_matrix_load(sg, sub_b, accB.get_pointer(), N);
          //CHECK: tail call { float, float, float, float, float, float, float, float } @llvm.nvvm.wmma.m16n16k16.load.c.row.stride.f32.p1f32(float addrspace(1)* %_arg_accC, i32 16)
          //CHECK-OPAQUE: tail call { float, float, float, float, float, float, float, float } @llvm.nvvm.wmma.m16n16k16.load.c.row.stride.f32.p1(ptr addrspace(1) %_arg_accC, i32 16)
          joint_matrix_load(sg, sub_c, accC.get_pointer(), N);

          // CHECK: tail call i32 @llvm.nvvm.f2tf32.rna(float {{.*}}
          // Round a, b to tf32
          for (auto i = 0; i < 4; ++i)
            sub_a.wi_marray[i] = round_to_tf32(sub_a.wi_marray[i]);

          for (auto i = 0; i < 4; ++i)
            sub_b.wi_marray[i] = round_to_tf32(sub_b.wi_marray[i]);

          //CHECK: tail call { float, float, float, float, float, float, float, float } @llvm.nvvm.wmma.m16n16k8.mma.row.row.tf32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 %{{.*}}, i32 {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}})
          sub_c = joint_matrix_mad(sg, sub_a, sub_b, sub_c);
          //CHECK: tail call void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.f32.p1f32(float addrspace(1)* {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, i32 {{.*}}
          //CHECK-OPAQUE: tail call void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.f32.p1(ptr addrspace(1) {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, i32 {{.*}}
          joint_matrix_store(sg, sub_c, accD.get_pointer(), N);
        });
  });

  q.submit([&](handler &cgh) {
    auto accA = bufA.get_access<access::mode::read_write>(cgh);
    auto accB = bufB.get_access<access::mode::read_write>(cgh);
    auto accC = bufC.get_access<access::mode::read_write>(cgh);
    auto accD = bufD.get_access<access::mode::read_write>(cgh);

    cgh.parallel_for<class col_col>(
        nd_range<2>({1, 32}, {1, 32}),
        [=](nd_item<2> item) [[sycl::reqd_work_group_size(1, 1, 32)]] {
          sycl::sub_group sg = item.get_sub_group();

          joint_matrix<precision::tf32, matrix_use::a, M, K,
                       matrix_layout::col_major>
              sub_a;

          joint_matrix<precision::tf32, matrix_use::b, K, N,
                       matrix_layout::col_major>
              sub_b;

          joint_matrix<float, matrix_use::accumulator, M, N,
                       matrix_layout::col_major>
              sub_c;

          //CHECK: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.a.col.stride.tf32.p0i32(i32* %call.ascast.i.i{{.*}}.i, i32 8)
          //CHECK-OPAQUE: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.a.col.stride.tf32.p0(ptr %call.ascast.i.i{{.*}}.i, i32 8)
          joint_matrix_load(sg, sub_a, accA.get_pointer(), K);
          //CHECK: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.b.col.stride.tf32.p0i32(i32* %call.ascast.i.i{{.*}}.i, i32 16)
          //CHECK-OPAQUE: tail call { i32, i32, i32, i32 } @llvm.nvvm.wmma.m16n16k8.load.b.col.stride.tf32.p0(ptr %call.ascast.i.i{{.*}}.i, i32 16)
          joint_matrix_load(sg, sub_b, accB.get_pointer(), N);
          //CHECK: tail call { float, float, float, float, float, float, float, float } @llvm.nvvm.wmma.m16n16k16.load.c.col.stride.f32.p1f32(float addrspace(1)* {{.*}}, i32 {{.*}})
          //CHECK-OPAQUE: tail call { float, float, float, float, float, float, float, float } @llvm.nvvm.wmma.m16n16k16.load.c.col.stride.f32.p1(ptr addrspace(1) {{.*}}, i32 {{.*}})
          joint_matrix_load(sg, sub_c, accC.get_pointer(), N);

          // CHECK: tail call i32 @llvm.nvvm.f2tf32.rna(float {{.*}}
          // Round a, b to tf32
          for (auto i = 0; i < 4; ++i)
            sub_a.wi_marray[i] = round_to_tf32(sub_a.wi_marray[i]);

          for (auto i = 0; i < 4; ++i)
            sub_b.wi_marray[i] = round_to_tf32(sub_b.wi_marray[i]);

          //CHECK: tail call { float, float, float, float, float, float, float, float } @llvm.nvvm.wmma.m16n16k8.mma.col.col.tf32(i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, i32 {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}})
          sub_c = joint_matrix_mad(sg, sub_a, sub_b, sub_c);
          //CHECK: tail call void @llvm.nvvm.wmma.m16n16k16.store.d.col.stride.f32.p1f32(float addrspace(1)* {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, i32 16)
          //CHECK-OPAQUE: tail call void @llvm.nvvm.wmma.m16n16k16.store.d.col.stride.f32.p1(ptr addrspace(1) {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, float {{.*}}, i32 16)
          joint_matrix_store(sg, sub_c, accD.get_pointer(), N);
        });
  });

  return 0;
};
