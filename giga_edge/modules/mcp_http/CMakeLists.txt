cmake_minimum_required(VERSION 3.14)

giga_edge_module(mcp_http
  DEPS assistant_core_module mcp_module web_module
  STUB_DEPS mcp_module web_module
)
