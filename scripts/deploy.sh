#!/bin/bash
rsync -avz --exclude 'build/' --exclude '.git/' ~/Development/hue_weather/ mikkanen@Raspberry5.local:~/Development/hue_weather/
# ssh mikkanen@Raspberry5.local "cd ~/Development/hue_weather/build && make -j$(nproc)"
