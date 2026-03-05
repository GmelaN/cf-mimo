build_lib_example(
  NAME cf-mimo-distributed-clustering-example
  SOURCE_FILES cf-mimo-distributed-clustering-example.cc
  LIBRARIES_TO_LINK ${libcf-mimo}
                    ${libcore}
                    ${libnetwork}
                    ${libinternet}
                    ${libpoint-to-point}
                    ${libapplications}
)

build_lib_example(
  NAME cf-mimo-centralized-clustering-example
  SOURCE_FILES cf-mimo-centralized-clustering-example.cc
  LIBRARIES_TO_LINK ${libcf-mimo}
                    ${libcore}
                    ${libnetwork}
                    ${libinternet}
                    ${libpoint-to-point}
                    ${libapplications}
)

build_lib_example(
  NAME cf-mimo-pilot-phy-example
  SOURCE_FILES cf-mimo-pilot-phy-example.cc
  LIBRARIES_TO_LINK ${libcf-mimo}
                    ${libcore}
                    ${libspectrum}
                    ${libpropagation}
                    ${libmobility}
)

build_lib_example(
  NAME cf-mimo-app-traffic-example
  SOURCE_FILES cf-mimo-app-traffic-example.cc
  LIBRARIES_TO_LINK ${libcf-mimo}
                    ${libcore}
                    ${libnetwork}
                    ${libinternet}
                    ${libpoint-to-point}
                    ${libapplications}
)

build_lib_example(
  NAME cf-mimo-control-storm-example
  SOURCE_FILES cf-mimo-control-storm-example.cc
  LIBRARIES_TO_LINK ${libcf-mimo}
                    ${libcore}
)
