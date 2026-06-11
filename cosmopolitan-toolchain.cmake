# -----------------------------
# Cosmopolitan CMake Toolchain
# -----------------------------
set(COSMOPOLITAN TRUE)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# --- Compilers ---
set(CMAKE_C_COMPILER cosmocc)
set(CMAKE_CXX_COMPILER cosmoc++)
set(CMAKE_ASM_COMPILER cosmocc)

# --- Archiver tools ---
set(CMAKE_AR cosmoar)
set(CMAKE_RANLIB cosmoranlib)
