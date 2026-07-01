cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Core Test Sql Network Concurrent REQUIRED)

set(CMAKE_AUTOMOC ON)

# Document Processor Test
add_executable(tst_document_processor tst_document_processor.cpp ../ohara-gui/src/document_processor.cpp ../ohara-gui/src/database_manager.cpp)
target_include_directories(tst_document_processor PRIVATE ../ohara-gui/src)
target_link_libraries(tst_document_processor PRIVATE Qt6::Core Qt6::Test Qt6::Sql)
add_test(NAME tst_document_processor COMMAND tst_document_processor)

# Model Manager Test
add_executable(tst_model_manager tst_model_manager.cpp ../ohara-gui/src/model_manager.cpp ../ohara-gui/src/settings_manager.cpp)
target_include_directories(tst_model_manager PRIVATE ../ohara-gui/src)
target_link_libraries(tst_model_manager PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME tst_model_manager COMMAND tst_model_manager)


# Inference Engine Test
add_executable(tst_inference_engine tst_inference_engine.cpp ../ohara-gui/src/inference_engine.cpp ../ohara-gui/src/LlamaBackend.cpp)
target_include_directories(tst_inference_engine PRIVATE ../ohara-gui/src ${llama_cpp_SOURCE_DIR}/include ${llama_cpp_SOURCE_DIR}/common ${llama_cpp_SOURCE_DIR}/ggml/include)
target_link_libraries(tst_inference_engine PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent llama common)
add_test(NAME tst_inference_engine COMMAND tst_inference_engine)
