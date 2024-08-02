#!/bin/bash

# 定义一个分支列表
branches=("util" "syscall" "pgtbl" "traps" "cow" "thread" "net" "lock" "fs" "mmap") 

# 保存当前分支名
current_branch=$(git branch --show-current)

for branch in "${branches[@]}"
do
  echo "Switching to branch $branch"

  # 暂存未提交的更改
  git stash push -m "Stashing changes before switching to $branch"

  git checkout $branch
  git pull origin $branch
  
  test_file="grade-lab-$branch"

  echo "Converting file format for $test_file"
  dos2unix ./$test_file
  
  echo "Changing file permissions for $test_file"
  sudo chmod +x ./$test_file
  
  echo "Running tests for $branch"
  make grade
  
  # 检查测试结果
  if [ $? -eq 0 ]; then
    echo "Tests passed on branch $branch"
  else
    echo "Tests failed on branch $branch"
  fi

  # 切回主分支并恢复暂存的更改
  git checkout $current_branch
  git stash pop
done