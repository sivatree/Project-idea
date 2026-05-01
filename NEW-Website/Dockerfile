FROM gcc:latest

RUN apt-get update && apt-get install -y python3

WORKDIR /app
COPY . .

# compile C
RUN gcc server.c -o server

# รัน server
CMD ["./server"]
