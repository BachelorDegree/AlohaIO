protoc --cpp_out=. hello.proto
protoc --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` hello.proto
