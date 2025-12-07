cmake_minimum_required(VERSION 3.5)
project(mod-material-bank)

set(scripts_STAT_SRCS
  src/autoupdate.cpp
  src/material_bank_loader.cpp
  src/material_bank_npc.cpp
  src/material_bank_hooks.cpp
  src/material_bank.cpp
  src/material_bank_commands.cpp
)

AC_ADD_SCRIPT("${scripts_STAT_SRCS}")
AC_ADD_CONFIG_FILE(conf/mod_material_bank.conf.dist)

message(STATUS "-> module 'mod-material-bank' loaded.")
