// Appended only to the isolated scheduler translation unit.
extern "C" const task* regression_task(int slot) {
    return g_task_table.at(slot);
}
extern "C" int regression_current_slot() {
    return current_task;
}
