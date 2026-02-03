#include "crow.h"
#include <sqlite3.h> 
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <map>
#include <fstream>
#include <sstream>
#include <array>
#include <memory>
#include <algorithm>
#include <random>

using namespace crow;

// ==========================================
// 1. DATA STRUCTURES & GLOBALS
// ==========================================

struct QuizQuestion {
    std::string question;
    std::vector<std::string> options;
    int correct_index;
};

// Global Database for CSV (Legacy)
std::map<std::string, std::vector<QuizQuestion>> database;

// Global SQLite Handler (User Auth)
sqlite3* db;

// ==========================================
// 2. DATABASE INIT (SQLite)
// ==========================================
void init_db() {
    int rc = sqlite3_open("users.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }
    // Create Users Table if it doesn't exist
    const char* sql = "CREATE TABLE IF NOT EXISTS users ("
                      "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "USERNAME TEXT UNIQUE NOT NULL,"
                      "PASSWORD TEXT NOT NULL);"; 
    char* errMsg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL Error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    } else {
        std::cout << "SUCCESS: User Database loaded." << std::endl;
    }
}

// ==========================================
// 3. CSV LOADER (Legacy Support)
// ==========================================
void load_csv_database() {
    std::ifstream file("questions.csv");
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string segment;
        std::vector<std::string> row;
        while (std::getline(ss, segment, ',')) row.push_back(segment);

        if (row.size() >= 7) {
            std::string subject = row[0];
            QuizQuestion q;
            q.question = row[1];
            q.options = {row[2], row[3], row[4], row[5]};
            try { q.correct_index = std::stoi(row[6]); } catch(...) { continue; }
            database[subject].push_back(q);
        }
    }
}

// ==========================================
// 4. AI ENGINE (ROBUST VERSION)
// ==========================================

std::string clean_json_string(std::string s) {
    std::string output;
    for (char c : s) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n' || c == '\r') output += " "; 
        else output += c;
    }
    return output;
}

std::string get_random_topic(std::string subject) {
    static std::map<std::string, std::vector<std::string>> topics = {
        {"Mathematics", {"Algebra", "Geometry", "Calculus", "Probability", "Mental Math", "Percentages"}},
        {"Physics", {"Newton's Laws", "Thermodynamics", "Optics", "Motion", "Gravity"}},
        {"Biology", {"Genetics", "Cell Biology", "Ecology", "Human Body", "Plants"}},
        {"History", {"World War II", "Ancient Civilizations", "The Renaissance", "Inventions", "Cold War"}},
        {"Computer Science", {"Python", "Binary Code", "Cybersecurity", "Hardware", "Internet"}}
    };

    if (topics.count(subject)) {
        std::vector<std::string>& list = topics[subject];
        return list[rand() % list.size()];
    }
    return subject;
}

QuizQuestion generate_ai_question(std::string subject, std::string difficulty) {
    std::string sub_topic = get_random_topic(subject);
    
    std::string prompt = "Output only valid JSON. Create a " + difficulty + " question about " + sub_topic + ". "
                         "Format: { \"q\": \"Question Text\", \"correct\": \"The Correct Answer\", \"wrong\": [\"Wrong1\", \"Wrong2\", \"Wrong3\"] }. "
                         "Ensure all options are in the same format (e.g. all percentages or all numbers).";

    std::cout << "\n[DEBUG] Asking AI for: " << sub_topic << "..." << std::endl;

    for(int attempt=0; attempt<3; attempt++) {
        std::string safe_prompt = clean_json_string(prompt);
        std::string cmd = "curl -s -X POST http://localhost:11434/api/generate -d '{\"model\": \"llama3.2\", \"prompt\": \"" + safe_prompt + "\", \"format\": \"json\", \"stream\": false, \"options\": {\"temperature\": 0.8}}'";
        
        std::array<char, 128> buffer;
        std::string result_json;
        std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
        
        if (!pipe) continue;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) result_json += buffer.data();

        auto json_wrapper = json::load(result_json);
        if (!json_wrapper || !json_wrapper.has("response")) continue;

        std::string raw_content = json_wrapper["response"].s();
        auto ai_data = json::load(raw_content);
        
        if (!ai_data) continue;

        try {
            QuizQuestion q;
            q.question = ai_data["q"].s();
            std::string correct_ans = ai_data["correct"].s();
            
            if (ai_data["wrong"].t() != json::type::List) continue;
            
            std::vector<std::string> temp_options;
            temp_options.push_back(correct_ans); 
            
            for(const auto& opt : ai_data["wrong"]) {
                temp_options.push_back(opt.s());
            }

            while(temp_options.size() < 4) temp_options.push_back("None");
            if(temp_options.size() > 4) temp_options.resize(4);

            // Shuffle Logic
            auto rng = std::default_random_engine(std::chrono::system_clock::now().time_since_epoch().count());
            std::shuffle(temp_options.begin(), temp_options.end(), rng);

            q.options = temp_options;
            q.correct_index = 0; 
            for(int i=0; i<4; i++) {
                if(q.options[i] == correct_ans) {
                    q.correct_index = i;
                    break;
                }
            }

            std::cout << "[DEBUG] Success! Q: " << q.question.substr(0, 30) << "... (Ans Index: " << q.correct_index << ")" << std::endl;
            return q;

        } catch (const std::exception& e) {
            std::cout << "[DEBUG] Parse Error: " << e.what() << std::endl;
        }
    }

    QuizQuestion backup;
    backup.question = "AI is resting. What is 5 + 5?";
    backup.options = {"8", "10", "12", "0"};
    backup.correct_index = 1;
    return backup;
}

std::string ask_ai_explanation(std::string question, std::string wrong_choice, std::string correct_choice) {
    std::string prompt = "Explain briefly why \"" + wrong_choice + "\" is wrong and \"" + correct_choice + "\" is correct for: " + question;
    std::string safe_prompt = clean_json_string(prompt);
    std::string cmd = "curl -s -X POST http://localhost:11434/api/generate -d '{\"model\": \"llama3.2\", \"prompt\": \"" + safe_prompt + "\", \"stream\": false}'";
    std::array<char, 128> buffer;
    std::string result_json;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "AI Connection Error";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) result_json += buffer.data();
    auto json_data = json::load(result_json);
    if (!json_data || !json_data.has("response")) return "AI Error";
    return json_data["response"].s();
}

// ==========================================
// 5. MAIN APPLICATION
// ==========================================

int main() {
    SimpleApp app;
    init_db();              // Initialize SQLite
    std::srand(std::time(0)); 
    load_csv_database();    // Load CSV questions

    // --- STATIC FILES (CSS, JS, Images, PWA Manifest) ---
    CROW_ROUTE(app, "/static/<string>")
    ([](const request& req, response& res, std::string filename){
        std::ifstream in("static/" + filename, std::ifstream::in);
        if(in){
            std::ostringstream contents; contents << in.rdbuf(); in.close();
            if(filename.find(".css") != std::string::npos) res.set_header("Content-Type", "text/css");
            else if(filename.find(".json") != std::string::npos) res.set_header("Content-Type", "application/json");
            res.write(contents.str());
        } else { res.code = 404; res.write("Not Found"); }
        res.end();
    });

    // --- SERVICE WORKER (For PWA Offline) ---
    CROW_ROUTE(app, "/sw.js")([](const request& req, response& res){
        res.set_header("Content-Type", "application/javascript");
        res.write("self.addEventListener('fetch', function(event){});"); // Basic SW
        res.end();
    });

    // --- HTML PAGES ---
    CROW_ROUTE(app, "/login")([](){ return mustache::load("login_pro.html").render(); });
    
    // *** FIXED: Shows Menu instead of Redirecting ***
    CROW_ROUTE(app, "/")([](){ return mustache::load("menu.html").render(); });
    
    // Game Pages
    CROW_ROUTE(app, "/math")([](){ return mustache::load("math_haat.html").render(); });
    CROW_ROUTE(app, "/physics")([](){ return mustache::load("physics_cricket.html").render(); });
    CROW_ROUTE(app, "/biology")([](){ return mustache::load("bio_farm.html").render(); });
    CROW_ROUTE(app, "/tech")([](){ return mustache::load("tech_tractor.html").render(); });
    CROW_ROUTE(app, "/history")([](){ return mustache::load("history_story.html").render(); });
    CROW_ROUTE(app, "/quiz")([](){ return mustache::load("quiz_party.html").render(); });

    // --- AUTH API (Register) ---
    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
    ([](const request& req){
        auto x = json::load(req.body);
        if(!x) return response(400);
        
        std::string u = x["u"].s();
        std::string p = x["p"].s();

        std::string sql = "INSERT INTO users (USERNAME, PASSWORD) VALUES ('" + u + "', '" + p + "');";
        char* errMsg = 0;
        int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg);

        json::wvalue res;
        if(rc != SQLITE_OK) {
            res["status"] = "error";
            res["message"] = "Username already exists!";
            sqlite3_free(errMsg);
        } else {
            res["status"] = "success";
        }
        return response(res);
    });

    // --- AUTH API (Login) ---
    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
    ([](const request& req){
        auto x = json::load(req.body);
        std::string u = x["u"].s();
        std::string p = x["p"].s();

        std::string sql = "SELECT PASSWORD FROM users WHERE USERNAME='" + u + "';";
        sqlite3_stmt* stmt;
        
        json::wvalue res;
        if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
            if(sqlite3_step(stmt) == SQLITE_ROW) {
                std::string db_pass = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if(db_pass == p) {
                    res["status"] = "success";
                } else {
                    res["status"] = "fail";
                    res["message"] = "Invalid Password";
                }
            } else {
                res["status"] = "fail";
                res["message"] = "User not found";
            }
        }
        sqlite3_finalize(stmt);
        return response(res);
    });

    // --- AI API (Get Question) ---
    CROW_ROUTE(app, "/api/ai/get_question")
    .methods("GET"_method)([](const request& req){
        std::string subject = req.url_params.get("subject");
        std::string diff = req.url_params.get("difficulty");
        if(subject.empty()) subject = "General Knowledge";
        if(diff.empty()) diff = "Medium";
        
        QuizQuestion q = generate_ai_question(subject, diff);
        
        json::wvalue x; 
        x["question"] = q.question;
        for(int i=0; i<4; i++) x["options"][i] = q.options[i];
        x["answer"] = q.correct_index;
        return x;
    });

    // --- AI API (Explain) ---
    CROW_ROUTE(app, "/api/ai/explain")
    .methods("POST"_method)([](const request& req){
        auto x = json::load(req.body);
        if (!x) return response(400);

        std::string q = x["question"].s(); 
        std::string w = x["wrong"].s(); 
        std::string c = x["correct"].s();
        
        json::wvalue res; 
        res["explanation"] = ask_ai_explanation(q, w, c); 
        return response(res);
    });

    app.port(8080).multithreaded().run();
    sqlite3_close(db); // Close DB on exit
}