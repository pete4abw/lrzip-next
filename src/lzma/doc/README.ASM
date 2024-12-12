June 2024

Recent source changes in 24.06/07 added 64 byte
alignment of code segments. These conditional changes
were not incorporated since the default x86_64
alignment is on 64 byte boundaries. No other code
changes to LzFindOpt or LzmaDecOpt were detected.

June-November 2021
Peter Hyman

Added file LzFindOpt.asm to speed up Match Finder.
Updates to LzmaDecOpt.asm

May 2020
Peter Hyman

Completed NASM Conversion.
Results in 30%+ speed improvement in decompression.

Thanks to Conor McCarthy, of fast_lzma2 for
inspiration in his code for helping get this
retarted.
Thanks to Igor Pavlov for his assistance in
debugging.

December 2019
Peter Hyman

This file is MASM syntax and not NASM.
Need help to convert this to nasm/yasm
code.
