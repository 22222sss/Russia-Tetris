#pragma once
#include <iostream>
#include <sys/resource.h>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <string>
#include <memory>

class SystemOptimizer {
public:
    static bool optimizeForHighConcurrency();
    static void printCurrentLimits();

private:
    static bool setFileDescriptorLimit();
    static bool checkTCPOptimization();
    static void printSystemInfo();
    static bool isInContainer();
    static void printRecommendations();
};
