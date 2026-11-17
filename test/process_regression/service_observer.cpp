// Appended only to the isolated file-service translation unit.
extern "C" size_t regression_service_in_use() {
    return tinyos::fs::g_open_files.in_use();
}
