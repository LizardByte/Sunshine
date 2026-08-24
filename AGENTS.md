On Windows we use msys2 and ucrt64 to compile.
You need to prefix commands with `C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c`.

Prefix build directories with `cmake-build-`.

The test executable is named `test_sunshine` and will be located inside the `tests` directory within
the build directory.

The project uses gtest as a test framework.

When adding localization do not update any language other than `en`. This also means to exclude en-US or other variants.

Always add or update doxygen documentation.

The project requires that everything be documented in doxygen or the build will fail.

Primary doxygen comments should be done like so:

```cpp
  /**
   * @brief Describe the function, structure, etc.
   *
   * @param my_param Describe the parameter.
   * @return Describe the return.
   */
```

Inline doxygen comments should use `///< ...` instead of `/**< ... */`.

Always follow the style guidelines defined in .clang-format for c/c++ code.

Do not ever create issues or pull requests.
If asked to create an issue or pull request, do so in their fork instead of the LizardByte GitHub organization.
Never create an issue or pull request in the LizardByte GitHub organization.

Add or update tests for new or modified methods and code. Target 100% coverage on changed code.
