#!/usr/bin/env bash

RETURN_PATH=$(pwd)
SCRIPT_PATH=$(dirname "$(readlink -f "$0")")

cd $SCRIPT_PATH/../../
make clean && make linux64
cd $SCRIPT_PATH

c_files=$(find "." -type f -name "*.c" | sort)

for file in $c_files; do
  filename=$(basename "$file" .c)
  clang -g -o "./$filename" "$file" -L../../build -lwnp_linux_amd64 -I../../include

  if [ $? -eq 0 ]; then
    echo "Compiled $file successfully"
    "./$filename"
    exit_code=$?

    if [ $exit_code -eq 0 ]; then
      echo "Test succeeded: $filename"
    else
      echo "Test failed with exit code $exit_code: $filename"
      exit 1
    fi
  else
    echo "Compilation failed for $file"
  fi
done

cd $RETURN_PATH