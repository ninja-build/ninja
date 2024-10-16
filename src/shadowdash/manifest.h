#pragma once

#include <string_view>
#include <initializer_list>
#include <vector>
#include <unordered_map>
#include <optional>
#include <iostream>

namespace shadowdash {

    class Expression {
    public:
        enum class Type { CONSTANT, VARIABLE };

        constexpr Expression(Type type, std::string_view content)
            : type_(type), content_(content) {}

        Type type_;
        std::string_view content_;
    };

    constexpr Expression constant(const char* value, std::size_t len) {
        return Expression(Expression::Type::CONSTANT, {value, len});
    }

    constexpr Expression variable(const char* value, std::size_t len) {
        return Expression(Expression::Type::VARIABLE, {value, len});
    }

    class Command {
    public:
        Command(std::initializer_list<Expression> parts) : parts_(parts) {}
        std::initializer_list<Expression> parts_;
    };

    class Rule {
    public:
        Rule(Command command) : command_(command) {}
        Command command_;
        std::optional<std::string_view> description_;
        std::optional<std::string_view> depfile_;
        std::optional<std::string_view> deps_;
        std::optional<std::string_view> generator_;
        std::optional<std::string_view> restat_;
        std::optional<std::string_view> rspfile_;
        std::optional<std::string_view> rspfile_content_;
        std::optional<std::string_view> pool_;
    };

    class Build {
    public:
        Build(std::string_view output, 
            std::string_view rule, 
            std::initializer_list<std::string_view> inputs,
            std::initializer_list<std::string_view> implicit_inputs = {},
            std::initializer_list<std::string_view> order_only_inputs = {},
            std::initializer_list<std::string_view> implicit_outputs = {},
            bool is_phony = false)
            : output_(output), rule_(rule), inputs_(inputs), 
            implicit_inputs_(implicit_inputs), order_only_inputs_(order_only_inputs),
            implicit_outputs_(implicit_outputs), is_phony_(is_phony) {}

        bool is_phony_ = false;
        std::string_view output_;
        std::string_view rule_;
        std::vector<std::string_view> inputs_;
        std::vector<std::string_view> implicit_inputs_;
        std::vector<std::string_view> order_only_inputs_;
        std::vector<std::string_view> implicit_outputs_;
        std::unordered_map<std::string_view, std::string_view> variables_;
    };

    class ShadowDash {
    private:
        std::unordered_map<std::string_view, Rule> rules_;
        std::vector<Build> builds_;
        std::unordered_map<std::string_view, std::string_view> variables_;
        std::vector<std::string_view> defaults_;
        std::optional<std::string_view> builddir_;

    public:
        void defineRule(std::string_view name, Rule rule) {
            rules_.emplace(name, std::move(rule));
        }

        void defineBuild(Build build) {
            builds_.push_back(std::move(build));
        }

        void defineVariable(std::string_view name, std::string_view value) {
            variables_[name] = value;
        }

        void addDefault(std::string_view target) {
            defaults_.push_back(target);
        }

        void setBuildDir(std::string_view dir) {
            builddir_ = dir;
        }

void executeBuild() const {
    for (const auto& build : builds_) {
        if (build.is_phony_) {
            std::cout << "Executing phony target: " << build.output_ << "\n";
        } else {
            std::cout << "Building: " << build.output_ << "\n";
            
            // Construct the command
            std::string command = "";
            for (const auto& part : rules_.at(build.rule_).command_.parts_) {
                if (part.type_ == Expression::Type::CONSTANT) {
                    command += std::string(part.content_);
                } else {
                    // Resolve variables
                    if (part.content_ == "in") {
                        command += build.inputs_[0];
                    } else if (part.content_ == "out") {
                        command += build.output_;
                    } else if (variables_.find(part.content_) != variables_.end()) {
                        command += variables_.at(part.content_);
                    } else {
                        command += "$" + std::string(part.content_);
                    }
                }
                command += " ";
            }
            
            // Execute the command
            std::cout << "Executing: " << command << "\n";
            int result = std::system(command.c_str());
            if (result != 0) {
                std::cerr << "Build command failed: " << command << "\n";
            }
        }
        std::cout << "\n";
    }
}
    };

    static constexpr auto in = "in";
    static constexpr auto out = "out";

} 


#define RULE(name, ...) \
    shadowDash.defineRule(#name, shadowdash::Rule{ \
        shadowdash::Command{ __VA_ARGS__ } \
    })

#define BUILD(...) \
    shadowDash.defineBuild(shadowdash::Build{ __VA_ARGS__ })

#define VAR(name, value) \
    shadowDash.defineVariable(#name, value)

#define DEFAULT(target) \
    shadowDash.addDefault(target)

#define BUILDDIR(dir) \
    shadowDash.setBuildDir(dir)

#define PHONY(name, rule, ...) \
    shadowDash.defineBuild(shadowdash::Build{#name, rule, {__VA_ARGS__}, {}, {}, {}, true})


