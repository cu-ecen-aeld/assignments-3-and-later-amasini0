# Faulty oops analysis

## What happened?
This is explained at line 2 in `faulty_oops.txt`, which reads

`Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000`

This means that there was a null pointer access, and a subsequent page fault that triggered the oops message.

## Where did it occur?
This is shown at line 19 of `faulty_oops.txt`, which reads
```
pc : faulty_write+0x10/0x20 [faulty]
```

This informs us that the oops was triggered in the `faulty_write` function, contained in module `faulty`; more precisely, by the instruction placed at byte 0x10 = 16 (over a total length of the function 0x20 = 32 bytes).

Using objdump on `faulty.ko` (the one under the `buildroot/output/target/...`) we get the content placed in `faulty_objdump.txt`. From this we can see that the line triggering the oops corresponds to the instruction 
```
  10:	b900003f 	str	wzr, [x1]
```
since the `faulty_write` function starts at address `0x0`. This instruction tries to store the contents of the `wzr` register in the memory location pointed by the value in the `x1` register.

The problem lies in the fact that the `x1` register was previously set to contain the value `0x0` (null pointer), as we can see from the instruction
```
   0:	d2800001 	mov	x1, #0x0                   	// #0
```
in `faulty_write` objdump. This triggers a page fault and thus the oops message.




