# SPDX-License-Identifier: Apache-2.0

# CMake 4 removed implicit compatibility with projects declaring a policy
# baseline older than 3.5.  The OpenAMP/libmetal copies in the current NuttX
# workspace still declare that historical baseline.  Select the documented
# compatibility floor from the board-owned custom toolchain hook so the
# standard openvela --cmake path works without modifying OpenAMP or NuttX.

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND
   NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
