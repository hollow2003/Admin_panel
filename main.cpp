#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "httplib.h"
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdio>
#include <future>
#include "json.hpp"
#include <algorithm>

using json = nlohmann::json;

// 函数用于获取HTTP响应数据
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// 函数用于发送HTTP请求并返回响应结果
std::string get_availible_device(const std::string& url) {
    std::string host = "127.0.0.1";
    std::string path = "/get_devices";
    int port = 8080;

    httplib::Client cli(host.c_str(), port);
    auto res = cli.Get(path.c_str());

    if (res && res->status == 200) {
        return res->body;
    } else {
        std::cerr << "HTTP request failed with status code: " << (res ? std::to_string(res->status) : "no response") << std::endl;
        return "Request failed";
    }
}

void initializeDeviceList(std::vector<std::string>& deviceNames, std::unordered_map<std::string, bool>& deviceStates) {
    std::string responseText = get_availible_device("http://127.0.0.1:8080/get_devices");
    std::cout << "Response: " << responseText << std::endl;

    // 解析JSON响应
    try {
        auto jsonResponse = json::parse(responseText);
        deviceNames.clear();
        for (const auto& device : jsonResponse["host"]) {
            deviceNames.push_back(device.get<std::string>());
            deviceStates[device.get<std::string>()] = false; // 初始化选中状态为false
        }
        // 对设备名称进行排序
        std::sort(deviceNames.begin(), deviceNames.end());
    } catch (const std::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
    }
}

bool deviceStatesChanged(const std::unordered_map<std::string, bool>& oldStates, const std::unordered_map<std::string, bool>& newStates) {
    for (const auto& [key, newValue] : newStates) {
        auto oldValueIt = oldStates.find(key);
        if (oldValueIt == oldStates.end() || oldValueIt->second != newValue) {
            return true;
        }
    }
    return false;
}

std::string getSelectedDeviceTopic(const std::unordered_map<std::string, bool>& deviceStates) {
    json requestJson;
    for (const auto& [device, selected] : deviceStates) {
        if (selected) {
            requestJson["hosts"].push_back(device);
        }
    }

    if (!requestJson["hosts"].empty()) {
        httplib::Client cli("http://127.0.0.1:8080");
        auto res = cli.Post("/get_api", requestJson.dump(), "application/json");
        if (res && res->status == 200) {
            return res->body;
        } else {
            std::cerr << "HTTP request failed with status code: " << (res ? std::to_string(res->status) : "no response") << std::endl;
            return "Request failed";
        }
    } else {
        return "No device selected";
    }
}

bool isValidJson(const std::string& str) {
    try {
        auto json = json::parse(str);
        return true;
    } catch (...) {
        return false;
    }
}

void UpdateDeviceTopicConfigToServer(const std::string& device, const std::unordered_map<std::string, std::string>& detail, const std::string& modifiedField) {
    httplib::Client cli("http://127.0.0.1:8080");
    json detailJson;
    std::string url;
    std::string body;
    if (modifiedField == "proxy") {
        bool proxy = std::stoi(detail.at("proxy"));
        detailJson["host_name"] = device;
        detailJson["API"]["address"] = detail.at("address");
        if (proxy) {
            url = "/add_proxy";
        } else {
            url = "/delete_proxy";
        }
        body = json::array({detailJson}).dump();
    } else {
        json interestTopic;
        bool interested = std::stoi(detail.at("interested"));
        interestTopic["API"]["address"] = detail.at("address");
        if (interested == 1){
            interestTopic["cycle"] = std::stoi(detail.at("cycle"));
            url = "/add_interest_topic";
        }
        else{
            url = "/cancel_interest_topic";
        }
        detailJson["interest_topic"] = json::array({interestTopic});
        detailJson["host_name"] = device;
        json requestJson;
        requestJson["interest"] = json::array({detailJson});
        body = requestJson.dump();
    }
    std::cout << "Request URL: " << url << "\nRequest Body: " << body << std::endl;
    auto res = cli.Post(url, body, "application/json");
    if (!res || res->status != 200) {
        std::cerr << "Failed to update data: " << (res ? res->status : 0) << std::endl;
    }
}

int main() {
    // 初始化GLFW
    if (!glfwInit()) {
        return -1;
    }

    // 设置GLFW窗口提示
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    std::string DeviceResponse = "No response yet";
    std::string selectedDeviceTopic = "No topic available";
    std::future<std::string> DeviceResponseFuture;
    std::unordered_map<std::string, bool> previousDeviceStates;
    std::unordered_map<std::string, bool> deviceStates;
    std::vector<std::string> deviceNames;
    previousDeviceStates = deviceStates;
    initializeDeviceList(deviceNames, deviceStates);
    std::unordered_map<std::string, std::unordered_map<std::string, int>> previousCycleStates;
    bool modify = true;
    // 创建窗口并设置窗口大小
    GLFWwindow* window = glfwCreateWindow(1280, 1280, "Admin panel", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // 启用垂直同步

    // 初始化GLEW
    if (glewInit() != GLEW_OK) {
        return -1;
    }

    // 初始化ImGui
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 主循环
    while (!glfwWindowShouldClose(window)) {
        // 处理事件
        glfwPollEvents();

        // 开始ImGui帧
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        // 添加一个窗口
        ImGui::Begin("Available device list");

        // 添加一个带滚动条的文本框
        ImGui::BeginChild("Scrolling");
        if (!deviceNames.empty()) {
            for (const auto& name : deviceNames) {
                ImGui::Checkbox(name.c_str(), &deviceStates[name]);
            }
        } else {
            ImGui::Text("No Available Device,Please Refresh");
        }
        ImGui::EndChild();
        // 添加一个按钮
        if (ImGui::Button("Send Request")) {
            // 当按钮被点击时发送HTTP请求
            std::cout << "Button clicked" << std::endl;
            DeviceResponseFuture = std::async(std::launch::async, get_availible_device, "http://127.0.0.1:8080/get_devices");
        }
        ImGui::End();

        // 检查异步任务是否完成
        if (DeviceResponseFuture.valid() && DeviceResponseFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            DeviceResponse = DeviceResponseFuture.get();
            std::cout << "Response: " << DeviceResponse << std::endl;
            try {
                auto jsonResponse = nlohmann::json::parse(DeviceResponse);
                deviceNames.clear();
                for (const auto& device : jsonResponse["host"]) {
                    deviceNames.push_back(device);
                    deviceStates[device.get<std::string>()] = false;
                }
                std::sort(deviceNames.begin(), deviceNames.end());
            } catch (const std::exception& e) {
                std::cerr << "JSON parsing error: " << e.what() << std::endl;
            }
        }
        if (deviceStatesChanged(previousDeviceStates, deviceStates)) {
            selectedDeviceTopic = getSelectedDeviceTopic(deviceStates);
            previousDeviceStates = deviceStates; // 更新之前的设备选中状态
            modify = true;
        }
        static std::unordered_map<std::string, std::vector<std::unordered_map<std::string, std::string>>> editableData;

        // 添加一个新的文本框，显示选中设备的topic
        if (selectedDeviceTopic != "No topic available" && isValidJson(selectedDeviceTopic)) {
            // 处理 selectedDeviceTopic
            try {
                if (modify == true){
                    selectedDeviceTopic = getSelectedDeviceTopic(deviceStates);
                    previousDeviceStates = deviceStates;
                    auto jsonResponse = json::parse(selectedDeviceTopic);
                    editableData.clear();
                    for (const auto& [device, details] : jsonResponse.items()) {
                        for (const auto& detail : details) {
                            std::unordered_map<std::string, std::string> detailMap;
                            detailMap["address"] = detail["address"].get<std::string>();
                            detailMap["interested"] = std::to_string(detail["interested"].get<int>());
                            detailMap["cycle"] = detail.contains("cycle") ? std::to_string(detail["cycle"].get<int>()) : "1";
                            detailMap["proxy"] = std::to_string(detail["proxy"].get<int>());
                            previousCycleStates[device][detail["address"]] = std::stoi(detailMap["cycle"]);
                            editableData[device].push_back(detailMap);
                            previousCycleStates[device][detail["address"]] = std::stoi(detailMap["cycle"]);
                        }
                    }
                    modify = false;
                }
                ImGui::SetNextWindowPos(ImVec2(850, 650), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
                ImGui::Begin("Device Topic Table");
                if (ImGui::BeginTable("Topics", 5)) {
                    ImGui::TableSetupColumn("Device");
                    ImGui::TableSetupColumn("API");
                    ImGui::TableSetupColumn("Interested");
                    ImGui::TableSetupColumn("Cycle(/s)");
                    ImGui::TableSetupColumn("Proxy");
                    ImGui::TableHeadersRow();
                    for (auto& [device, details] : editableData) {
                        for (auto& detail : details) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", device.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", detail["address"].c_str());
                            ImGui::TableSetColumnIndex(2);
                            bool interested = std::stoi(detail["interested"]);
                            if (ImGui::Checkbox(("##interested" + device + detail["address"]).c_str(), &interested)) {
                                detail["interested"] = std::to_string(interested);
                                std::future<void> fut1 = std::async(std::launch::async, UpdateDeviceTopicConfigToServer, device, detail, "interested");
                                fut1.wait();
                                modify = true;
                            }
                            ImGui::TableSetColumnIndex(3);
                            if (interested == 1) {
                                int cycle = std::stoi(detail["cycle"]);
                                ImGui::InputInt(("##cycle" + device + detail["address"]).c_str(), &cycle);
                                if (cycle != previousCycleStates[device][detail["address"]]) {
                                        if (cycle <= 0) {
                                            cycle = 1; // 防止负值
                                        }
                                        detail["cycle"] = std::to_string(cycle);
                                        if (cycle != previousCycleStates[device][detail["address"]]) {
                                            previousCycleStates[device][detail["address"]] = cycle;
                                            std::future<void> fut2 = std::async(std::launch::async, UpdateDeviceTopicConfigToServer, device, detail, "cycle");
                                            fut2.wait();
                                            modify = true;
                                        }
                                }
                            } else {
                                ImGui::Text("1");
                            }
                            ImGui::TableSetColumnIndex(4);
                            bool proxy = std::stoi(detail["proxy"]);
                            if (ImGui::Checkbox(("##proxy" + device + detail["address"]).c_str(), &proxy)) {
                                detail["proxy"] = std::to_string(proxy);
                                std::future<void> fut3 = std::async(std::launch::async, UpdateDeviceTopicConfigToServer, device, detail, "proxy");
                                fut3.wait();
                                modify = true;
                            }
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::End();
            }catch (const std::exception& e) {
                std::cerr << "JSON parsing error: " << e.what() << std::endl;
            }
        }
        // 渲染ImGui
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // 交换缓冲
        glfwSwapBuffers(window);
    }

    // 清理ImGui资源
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // 清理GLFW资源
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}