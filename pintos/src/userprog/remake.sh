#!/bin/bash

make clean
make
cd build/
pintos-mkdisk filesys.dsk --filesys-size=2
cd ..

echo "userprog/ clean & rebuild."