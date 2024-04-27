git branch --show-current >> temp_file.h
set /p branchname=<temp_file.h
set branchmacro=#define BRANCH_NAME "%branchname%"
echo #pragma once > src/Branch.h
echo %branchmacro% >> src/Branch.h
del temp_file.h
