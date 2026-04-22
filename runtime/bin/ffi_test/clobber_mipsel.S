.text

.globl _ClobberAndCall
_ClobberAndCall:
.globl ClobberAndCall
ClobberAndCall:

/*Save link register*/
addiu   $sp, $sp, -4
sw      $ra, 0($sp)
/*Save frame pointer register*/
addiu   $sp, $sp, -4
sw      $fp, 0($sp)
/*Save thread register*/
addiu   $sp, $sp, -4
sw      $s3, 0($sp)
/*Save code pointer register*/
addiu   $sp, $sp, -4
sw      $s6, 0($sp)
/*Save pool pointer  register*/
addiu   $sp, $sp, -4
sw      $s7, 0($sp)

li  $ra, 1
li  $fp, 1
li  $s3, 1
li  $s6, 1
li  $s7, 1

/* Clobber all other volatile registers (except the argument). */
li  $v0, 1
li  $v1, 1
li  $t0, 1
li  $t1, 1
li  $t2, 1
li  $t3, 1
li  $t4, 1
li  $t5, 1
li  $t6, 1
li  $t7, 1
li  $a1, 1
li  $a2, 1
li  $a3, 1

/* Align the frame to 8 bytes. */
addiu   $sp, $sp, -4

jalr $a0

addiu   $sp, $sp, 4

lw      $s7, 0($sp)
addiu   $sp, $sp, 4
lw      $s6, 0($sp)
addiu   $sp, $sp, 4
lw      $s3, 0($sp)
addiu   $sp, $sp, 4
lw      $fp, 0($sp)
addiu   $sp, $sp, 4
lw      $ra, 0($sp)
addiu   $sp, $sp, 4

jr $ra
