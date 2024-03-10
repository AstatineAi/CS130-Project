#!/bin/bash

make clean
bear -- make
mv compile_commands.json build/
cd build/
pintos-mkdisk filesys.dsk --filesys-size=2
cd ..

echo "userprog/ clean & rebuild."
