set(LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

set(LIB_SRC
    address.cc
    config.cc
    daemon.cc
    env.cc
    fdmanager.cc
    fiber.cc
    hook.cc
    iomanager.cc
    log.cc
    mutex.cc
    scheduler.cc
    socket.cc
    tcpserver.cc
    thread.cc
    timer.cc
    util.cc
    )


add_library(zlar SHARED ${LIB_SRC})

