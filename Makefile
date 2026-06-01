.PHONY: configure build test run backtest clean \
        kill-switch-drill simulator-drill smoke-net \
        format tidy \
        docker-build docker-shell docker-test docker-smoke

BUILD_DIR ?= build
BUILD_TYPE ?= Release
VCPKG_TOOLCHAIN ?= $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
PROFILE ?= beta
IMAGE ?= propr-agent
DOCKER ?= docker
DOCKER_RUN = $(DOCKER) run --rm -it \
  -v "$$(pwd):/work" \
  -v propr-vcpkg:/opt/vcpkg/installed \
  --env-file .env \
  -e PROPR_PROFILE=$(PROFILE) \
  $(IMAGE)

configure:
	cmake -B $(BUILD_DIR) -S . \
	  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	  -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN)

build: configure
	cmake --build $(BUILD_DIR) -j

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

kill-switch-drill: build
	$(BUILD_DIR)/tests/test_kill_switch_drill

simulator-drill: build
	$(BUILD_DIR)/tests/test_simulator_drill

run: build
	@if [ "$(PROFILE)" = "live" ] && [ "$(CONFIRM_LIVE)" != "I_UNDERSTAND_THIS_CAN_TRADE" ]; then \
	  echo "Refusing to run against LIVE without explicit confirmation."; \
	  echo "Set CONFIRM_LIVE=I_UNDERSTAND_THIS_CAN_TRADE on the make command line."; \
	  exit 1; \
	fi
	$(BUILD_DIR)/app/propr_agent --config config/runtime.yaml --profile $(PROFILE)

backtest:
	@echo "Backtest binary is parked while we focus on demo-account validation."
	@echo "Re-enable add_subdirectory(backtest) in CMakeLists.txt to use it."

smoke-net: build
	@test -n "$$PROPR_API_KEY" || (echo "PROPR_API_KEY not set" && exit 1)
	$(BUILD_DIR)/app/propr_agent --smoke --profile $(PROFILE)

format:
	find include src strategies tests app \( -name '*.h' -o -name '*.cpp' \) \
	  -print0 | xargs -0 clang-format -i

tidy: build
	find src include -name '*.cpp' -o -name '*.h' \
	  | xargs clang-tidy -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

# --- Dockerized build (use this if vcpkg is not installed locally) ---

docker-build:
	$(DOCKER) build -t $(IMAGE) .

docker-shell:
	$(DOCKER_RUN) bash

docker-test:
	$(DOCKER_RUN) bash -lc "make test"

docker-smoke:
	$(DOCKER_RUN) bash -lc "make smoke-net PROFILE=$(PROFILE)"
