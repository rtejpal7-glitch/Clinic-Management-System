# ClinicFlow API server — multi-stage build.
# Stage 1 builds Oat++ 1.3.0, oatpp-swagger, libargon2 and the server from source.
# Stage 2 is a slim runtime with only the shared libraries the binary needs.

FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git ca-certificates libpq-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /deps
RUN git clone --depth 1 --branch 1.3.0 https://github.com/oatpp/oatpp.git \
 && cmake -S oatpp -B oatpp/build -DCMAKE_BUILD_TYPE=Release -DOATPP_BUILD_TESTS=OFF \
 && cmake --build oatpp/build -j"$(nproc)" && cmake --install oatpp/build
RUN git clone --depth 1 --branch 1.3.0 https://github.com/oatpp/oatpp-swagger.git \
 && cmake -S oatpp-swagger -B oatpp-swagger/build -DCMAKE_BUILD_TYPE=Release -DOATPP_BUILD_TESTS=OFF \
 && cmake --build oatpp-swagger/build -j"$(nproc)" && cmake --install oatpp-swagger/build
# argon2 defaults to -march=native, which tunes the library to the build machine's CPU.
# Hosted builders (Render, CI) often have newer CPUs than the servers that run the image,
# and the server then dies with "illegal instruction" (exit 132) on the first password hash.
# x86-64 is the portable baseline; on other architectures argon2 falls back to its reference code.
RUN git clone --depth 1 --branch 20190702 https://github.com/P-H-C/phc-winner-argon2.git \
 && make -C phc-winner-argon2 OPTTARGET=x86-64 libs \
 && cp phc-winner-argon2/libargon2.a /usr/local/lib/ && cp phc-winner-argon2/include/argon2.h /usr/local/include/
RUN git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git \
 && cp -r json/single_include/nlohmann /usr/local/include/

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY test ./test
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local \
 && cmake --build build -j"$(nproc)" \
 && ./build/clinicflow-unit-tests

FROM ubuntu:24.04 AS runtime
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends libpq5 libssl3 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home clinicflow
WORKDIR /app
COPY --from=build /src/build/clinicflow-server /app/clinicflow-server
COPY --from=build /deps/oatpp-swagger/res /app/swagger-res
COPY migrations /app/migrations
ENV MIGRATIONS_DIR=/app/migrations \
    SWAGGER_RES_PATH=/app/swagger-res \
    PORT=8000
USER clinicflow
EXPOSE 8000
HEALTHCHECK --interval=15s --timeout=3s --retries=5 CMD curl -fs "http://localhost:${PORT:-8000}/api/v1/health" || exit 1
ENTRYPOINT ["/app/clinicflow-server"]
