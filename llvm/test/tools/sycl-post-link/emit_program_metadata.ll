; This test checks that the post-link tool generates SYCL program metadata.
;
; RUN: sycl-post-link -emit-program-metadata -device-globals -S %s -o %t.files.table
; RUN: FileCheck %s -input-file=%t.files.table --check-prefixes CHECK-TABLE
; RUN: FileCheck %s -input-file=%t.files_0.prop --match-full-lines --check-prefixes CHECK-PROP

target triple = "spir64-unknown-unknown"

attributes #0 = { "sycl-work-group-size"="4,2,1" }
attributes #1 = { "sycl-work-group-size"="4,2" }
attributes #2 = { "sycl-work-group-size"="4" }

!0 = !{i32 1, i32 2, i32 4}
!1 = !{i32 2, i32 4}
!2 = !{i32 4}

define weak_odr spir_kernel void @SpirKernel1(float %arg1) !reqd_work_group_size !0 {
  call void @foo(float %arg1)
  ret void
}

define weak_odr spir_kernel void @SpirKernel2(float %arg1) #0 {
  call void @foo(float %arg1)
  ret void
}

define weak_odr spir_kernel void @SpirKernel3(float %arg1) !reqd_work_group_size !1 {
  call void @foo(float %arg1)
  ret void
}

define weak_odr spir_kernel void @SpirKernel4(float %arg1) #1 {
  call void @foo(float %arg1)
  ret void
}

define weak_odr spir_kernel void @SpirKernel5(float %arg1) !reqd_work_group_size !2 {
  call void @foo(float %arg1)
  ret void
}

define weak_odr spir_kernel void @SpirKernel6(float %arg1) #2 {
  call void @foo(float %arg1)
  ret void
}

declare void @foo(float)

; CHECK-PROP: [SYCL/program metadata]
; // Base64 encoding in the prop file (including 8 bytes length):
; CHECK-PROP-NEXT: SpirKernel1@reqd_work_group_size=2|gBAAAAAAAAQAAAAACAAAAQAAAAA
; CHECK-PROP-NEXT: SpirKernel2@reqd_work_group_size=2|gBAAAAAAAAQAAAAACAAAAQAAAAA
; CHECK-PROP-NEXT: SpirKernel3@reqd_work_group_size=2|ABAAAAAAAAgAAAAAEAAAAA
; CHECK-PROP-NEXT: SpirKernel4@reqd_work_group_size=2|ABAAAAAAAAgAAAAAEAAAAA
; CHECK-PROP-NEXT: SpirKernel5@reqd_work_group_size=2|gAAAAAAAAAABAAAA
; CHECK-PROP-NEXT: SpirKernel6@reqd_work_group_size=2|gAAAAAAAAAABAAAA

; CHECK-TABLE: [Code|Properties]
; CHECK-TABLE-NEXT: {{.*}}files_0.prop
; CHECK-TABLE-EMPTY:
