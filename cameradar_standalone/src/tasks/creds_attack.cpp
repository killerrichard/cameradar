// Copyright 2016 Etix Labs
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cachemanager.h>
#include <tasks/creds_attack.h>

namespace etix {
namespace cameradar {

static const std::string no_ids_warning_ =
    "The ids.json files' default paths didn't match with the discovered "
    "cameras. Either "
    "they have custom ids, or your ids.json file does not contain enough "
    "default routes. "
    "Path dictionary attack is impossible without the credentials.";

// Tries to match the detected combination of Username / Password
// with the camera stream. Creates a resource in the DB upon
// valid discovery
bool
creds_attack::test_ids(const etix::cameradar::stream_model& stream,
                    const std::string& password,
                    const std::string& username) const {
    bool found = false;
    std::string path = stream.service_name + "://";
    if (username != "" || password != "") { path += username + ":" + password + "@"; }
    path += stream.address + ":" + std::to_string(stream.port) + stream.route;
    LOG_INFO_("Testing ids : " + path, "creds_attack");
    try {
        if (curl_describe(path, true)) {
            LOG_INFO_("[FOUND IDS] : " + path, "creds_attack");
            found = true;
            stream_model newstream{
                stream.address, stream.port,         username,       password,
                stream.route,   stream.service_name, stream.product, stream.protocol,
                stream.state,   stream.path_found,   true,           stream.thumbnail_path
            };
            if ((*cache)->has_changed(stream)) return true;
            (*cache)->update_stream(newstream);
        } else {
            stream_model newstream{ stream.address,    stream.port,     username,
                                    password,          stream.route,    stream.service_name,
                                    stream.product,    stream.protocol, stream.state,
                                    stream.path_found, false,           stream.thumbnail_path };
            if ((*cache)->has_changed(stream)) return true;
            (*cache)->update_stream(newstream);
        }
    } catch (const std::runtime_error& e) {
        LOG_DEBUG_("Ids already tested : " + std::string(e.what()), "creds_attack");
    }
    return found;
}

bool
ids_already_found(std::vector<stream_model> streams, stream_model stream) {
    for (const auto& it : streams) {
        if ((stream.address == it.address) && (stream.port == it.port) && it.ids_found) return true;
    }
    return false;
}

bool
creds_attack::attack_camera_creds(const stream_model& stream) const {
    for (const auto& username : conf.usernames) {
        if (signal_handler::instance().should_stop() != etix::cameradar::stop_priority::running)
            break;
        for (const auto& password : conf.passwords) {
            if (signal_handler::instance().should_stop() != etix::cameradar::stop_priority::running)
                break;
            if ((*cache)->has_changed(stream)) return true;
            if (test_ids(stream, password, username)) return true;
        }
    }
    return false;
}

// Tries to discover the right IDs on all RTSP streams in DB
// Uses the ids.json file to try different combinations
bool
creds_attack::run() const {
    std::vector<std::future<bool>> futures;

    LOG_INFO_(
        "Beginning attack of the credentials , it may "
        "take a while.",
        "creds_attack");
    std::vector<etix::cameradar::stream_model> streams = (*cache)->get_streams();
    LOG_DEBUG_("Found " + std::to_string(streams.size()) + " streams in the cache", "creds_attack");
    size_t found = 0;
    for (const auto& stream : streams) {
        if (signal_handler::instance().should_stop() != etix::cameradar::stop_priority::running)
            break;
        if ((found < streams.size()) && ids_already_found(streams, stream)) {
            LOG_INFO_(stream.address +
                          " : This camera's ids were already discovered in "
                          "the database. Skipping to "
                          "the next camera.",
                      "creds_attack");
            ++found;
        } else {
            futures.push_back(
                std::async(std::launch::async, &creds_attack::attack_camera_creds, this, stream));
        }
    }
    for (auto& fit : futures) {
        if (fit.get()) { ++found; }
    }
    if (!found) {
        LOG_WARN_(no_ids_warning_, "creds_attack");
        return false;
    } else
        LOG_INFO_("Found " + std::to_string(found) + " ids for " + std::to_string(streams.size()) +
                      " cameras",
                  "creds_attack");
    return true;
}
}
}
