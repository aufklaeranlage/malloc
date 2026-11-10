*This project has been created as part of the 42 curriculum by abronner*

malloc
======

## Description

This is a rewrite of the libc malloc. The glibc malloc was used as a primary source to understand basics about memory caching, specific choke points to watch out for and potential systems to use for memory chunk lifecycle managements etc. Specific sources are listed below.

A major restriction of the subject is that we are not allowed to use `sbrk()` or `brk()` which creates the need for multiple arenas in which memory will be stored as we are only allowed to allocate memory via `mmap()`. A similar approach to glibc's malloc with a contingent section for the main arena would be possible with `mremap()`. Using `mremap()` or moving the mapped area after allocating a bigger chunk creates the big overhead of copying over arenas of potentially dozens of pagesizes.

One solution would be to create additional arenas that hold their own information about free chunks, so they can easily be checked for a potentiall unmapping, while still allowing a FastBin to have chunks spread over multiple arenas. Memory inside the arena's could be consolidated and split if needed.

If we use multiple arenas this means that for consilidation we would need either an indicator in a chunk whether it is the last chunk of an arena or be able to calculate this information via an offset to it's arena's base pointer and the chunk's size. In glibc's malloc a `malloc_state` contains a `mchunkptr` called `top` that points to the top most chunk of this arena. A similar solution can be used for a purely `mmap()` based malloc to iterate over the list of chunks based on their size until we hit the address saved in the arena's top variable.

## Instructions

## Resources

The source code for glibc's malloc was heavily sourced to understand existing versions and algorithms for malloc.

## Sources

[glibc malloc.c](https://codebrowser.dev/glibc/glibc/malloc/malloc.c.html)
[glibc malloc.h](https://codebrowser.dev/glibc/glibc/malloc/malloc.h.html)
[github glibc malloc.c](https://github.com/walac/glibc/blob/master/malloc/malloc.c)
