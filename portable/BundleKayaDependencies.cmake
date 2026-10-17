file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "/opt/KAYA_Instruments/lib/libKYFGLibA.so" "/opt/KAYA_Instruments/lib/libKYVPImageProcessing.so"
    DIRECTORIES "/opt/KAYA_Instruments/lib"
    RESOLVED_DEPENDENCIES_VAR kaya_resolved
    UNRESOLVED_DEPENDENCIES_VAR kaya_unresolved
)

foreach(dep IN LISTS kaya_resolved)
    if(dep MATCHES "^/opt/KAYA_Instruments/lib/")
        file(INSTALL DESTINATION "/home/xdlinx/SPIKEDISPLAY/SpikeDualDisplay_v1.4.1/portable/XDT-1.0.0-linux-x86_64/lib/kaya"
            TYPE SHARED_LIBRARY FILES "${dep}")
    endif()
endforeach()

if(kaya_unresolved)
    message(WARNING "Unresolved KAYA runtime dependencies: ${kaya_unresolved}")
endif()
