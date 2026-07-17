# screenTrans

**Easy way:**

1. Unzip this zip.
2. Unzip `msvc_run_time.zip`.
3. Copy all files from the extracted `msvc_run_time` folder and paste them into the folder that contains your `.exe` file.
4. You can now run the executable.

---

**To save space:**

The `msvc_run_time.zip` contains files you may not actually need. Consider these options:

- **If** you have Visual Studio (my version is 2026) or a MSVC compiler and can run the program without extracting these (since C++ is not ABI stable), simply delete this zip.
- **Else if** you cannot run, copy the files from `msvc_run_time.zip` to a specific folder and add that folder to the `PATH` environment variable.
- **Else** (i.e., you are not sure), I suggest you unzip it regardless of whether you can run it now, because the DLLs of other software may be removed in the future.
