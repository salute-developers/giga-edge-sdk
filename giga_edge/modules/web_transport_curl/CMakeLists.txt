cmake_minimum_required(VERSION 3.14)

module_enabled(web_transport_curl _wtc_enabled)
if(_wtc_enabled)
  # 7.86: SigV4 signs the x-amz-content-sha256 header that the transport sets (see curl_web_transport.cpp); older
  # versions sign without it, and S3 rejects every PUT with 403.
  # 7.87: CURLOPT_QUICK_EXIT (see CommitNow), used without a version guard.
  find_package(CURL 7.87 REQUIRED)
endif()

giga_edge_module(
  web_transport_curl
  DEPS
    web_module
  STUB_DEPS
    web_module
)

if(_wtc_enabled)
  target_link_libraries(web_transport_curl_module PRIVATE CURL::libcurl)

  # A static OpenSSL needs libdl on glibc < 2.34; FindOpenSSL does not always add it.
  # It is linked to the module: CURL::libcurl may be an ALIAS, which cannot be modified.
  if(CMAKE_DL_LIBS)
    target_link_libraries(web_transport_curl_module PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()
