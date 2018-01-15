# STMP media layer archive

This repository is an archive of the media layer that was developed for SigmaTel STMP devices. Is is placed
here for posterity.

The media layer provides a complete stack from a FAT32 filesystem to the low-level NAND flash translation layer and peripheral drivers. The most interesting part of the code is the [media/nand](media/nand) directory that contains
the wear-leveling NAND flash translation layer.

Note that the code is built to directly use the ThreadX RTOS API. It will have to be modified in order to work with
another RTOS.

This code is licensed with a BSD 3-clause license.
