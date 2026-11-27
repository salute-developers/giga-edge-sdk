file(READ "${CONFIG}" content)
string(REPLACE "${FROM}" "${TO}" content "${content}")
file(WRITE "${CONFIG}" "${content}")
