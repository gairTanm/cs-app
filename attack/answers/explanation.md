### ctarget1
- pad till it overflows and add the target address at the end
### ctarget2
- add `pop rdi; ret` object code at the beginning, reference it at the call stack pop location, add the value of cookie so rdi reads it and call `touch2`
### rtarget1
- similar to ctarget2, but use two gadgets, `pop rax; ret` (multiple options for this) put the cookie next ,`mov rax, rdi` and call `touch2`
