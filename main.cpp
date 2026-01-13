#include "crow.h"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <map>          
#include <fstream>
#include <sstream>

using namespace crow;

// ==========================================
// 1. DATA STRUCTURES & GLOBALS
// ==========================================

struct QuizQuestion {
    std::string question;
    std::vector<std::string> options;
    int correct_index;
};

// The Database: Subject -> List of Questions
std::map<std::string, std::vector<QuizQuestion>> database;

// ==========================================
// 2. CSV LOADER
// ==========================================

void load_csv_database() {
    std::ifstream file("questions.csv");
    
    if (!file.is_open()) {
        std::cerr << "WARNING: questions.csv not found! Quiz/History/Bio will be empty." << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string segment;
        std::vector<std::string> row;

        while (std::getline(ss, segment, ',')) {
            row.push_back(segment);
        }

        // Expect format: subject,question,opt1,opt2,opt3,opt4,ans_index
        if (row.size() >= 7) {
            std::string subject = row[0]; 
            
            QuizQuestion q;
            q.question = row[1];
            q.options = {row[2], row[3], row[4], row[5]};
            
            try {
                q.correct_index = std::stoi(row[6]);
                database[subject].push_back(q);
            } catch (...) { continue; }
        }
    }
    file.close();
    std::cout << "SUCCESS: Database loaded with " << database.size() << " subjects." << std::endl;
}

// ==========================================
// 3. MAIN APPLICATION
// ==========================================

int main() {
    SimpleApp app;
    std::srand(std::time(0)); 
    load_csv_database(); 

    // ==========================================
    // A. PAGE ROUTES (The "404 Not Found" Fix)
    // ==========================================
    
    // 1. Menu
    CROW_ROUTE(app, "/")([](){ return mustache::load("menu.html").render(); });

    // 2. Game Pages
    CROW_ROUTE(app, "/math")([](){ return mustache::load("math_haat.html").render(); });
    CROW_ROUTE(app, "/physics")([](){ return mustache::load("physics_cricket.html").render(); });
    CROW_ROUTE(app, "/biology")([](){ return mustache::load("bio_farm.html").render(); });
    CROW_ROUTE(app, "/tech")([](){ return mustache::load("tech_tractor.html").render(); });
    CROW_ROUTE(app, "/history")([](){ return mustache::load("history_story.html").render(); });
    CROW_ROUTE(app, "/quiz")([](){ return mustache::load("quiz_party.html").render(); });


    // ==========================================
    // B. CSV-BASED API (Bio, History, Quiz)
    // ==========================================

    CROW_ROUTE(app, "/api/get_question")
    .methods("GET"_method)([](const request& req){
        std::string subject = req.url_params.get("subject");
        
        // Validation: Does subject exist? Is it empty?
        if(database.find(subject) == database.end() || database[subject].empty()) {
             // If subject not found, try to pick a random one, or return error
             if(database.empty()) {
                 json::wvalue e; e["question"] = "Error: Database empty."; return e;
             }
             auto it = database.begin();
             std::advance(it, rand() % database.size());
             subject = it->first;
        }

        std::vector<QuizQuestion>& list = database[subject];
        int idx = rand() % list.size();
        
        json::wvalue x;
        x["subject"] = subject;
        x["question"] = list[idx].question;
        for(int i=0; i<4; i++) x["options"][i] = list[idx].options[i];
        x["answer"] = list[idx].correct_index;
        
        return x;
    });


    // ==========================================
    // C. LOGIC-BASED API (Math, Physics, Tech)
    // These do NOT use the CSV because they need calculation
    // ==========================================

    // Math Shopping
    CROW_ROUTE(app, "/api/math/problem")([](){
        json::wvalue x;
        std::vector<std::string> items = {"Potatoes", "Onions", "Rice", "Lentils", "Tomatoes"};
        int price = (rand() % 40) + 10; 
        int qty = (rand() % 5) + 1;     
        x["item"] = items[rand() % items.size()];
        x["price_per_kg"] = price;
        x["quantity"] = qty;
        x["correct_answer"] = price * qty;
        return x;
    });

    // Physics Projectile
    CROW_ROUTE(app, "/api/physics/shot")
    .methods("POST"_method)([](const request& req){
        auto x = json::load(req.body);
        if (!x) return response(400);
        double angle = x["angle"].d() * (M_PI / 180.0);
        double force = x["force"].d();
        double dist = (pow(force, 2) * sin(2 * angle)) / 9.8;
        json::wvalue res;
        res["distance"] = dist;
        if(dist > 70) res["result"] = "SIX! 🏏";
        else if(dist > 35) res["result"] = "FOUR! 🏃";
        else res["result"] = "CAUGHT! 👐";
        return response(res);
    });

    // Tech Grid Logic
    CROW_ROUTE(app, "/api/tech/run")
    .methods("POST"_method)([](const request& req){
        auto x = json::load(req.body);
        if (!x) return response(400);
        int r=0, c=0; 
        std::string status = "Lost";
        int grid[3][3] = {{0,0,0}, {1,1,0}, {0,0,3}};
        if(x.has("commands")){
            for (auto& cmd : x["commands"]) {
                int m = cmd.i();
                if(m==0) r--; else if(m==1) r++; else if(m==2) c--; else if(m==3) c++;
                if(r<0||r>2||c<0||c>2||grid[r][c]==1) { status = "CRASH"; break; }
                if(grid[r][c]==3) { status = "WIN"; break; }
            }
        }
        json::wvalue res; res["status"] = status;
        return response(res);
    });

    app.port(8080).multithreaded().run();
}