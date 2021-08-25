/*
Copyright Contributors to the libdnf project.

This file is part of libdnf: https://github.com/rpm-software-management/libdnf/

Libdnf is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

Libdnf is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libdnf.  If not, see <https://www.gnu.org/licenses/>.
*/


#include "group_info.hpp"

#include "../../context.hpp"

#include <libdnf/comps/comps.hpp>
#include <libdnf/comps/group/group.hpp>
#include <libdnf/comps/group/query.hpp>
#include <libdnf/conf/option_string.hpp>
#include "libdnf-cli/output/groupinfo.hpp"

#include <filesystem>
#include <fstream>
#include <set>


namespace microdnf {


using namespace libdnf::cli;


GroupInfoCommand::GroupInfoCommand(Command & parent) : Command(parent, "info") {
    auto & ctx = static_cast<Context &>(get_session());
    auto & parser = ctx.get_argument_parser();

    auto & cmd = *get_argument_parser_command();
    cmd.set_short_description("Print details about comps groups");

    available_option = dynamic_cast<libdnf::OptionBool *>(
        parser.add_init_value(std::unique_ptr<libdnf::OptionBool>(new libdnf::OptionBool(false))));

    installed_option = dynamic_cast<libdnf::OptionBool *>(
        parser.add_init_value(std::unique_ptr<libdnf::OptionBool>(new libdnf::OptionBool(false))));

    hidden_option = dynamic_cast<libdnf::OptionBool *>(
        parser.add_init_value(std::unique_ptr<libdnf::OptionBool>(new libdnf::OptionBool(false))));

    auto available = parser.add_new_named_arg("available");
    available->set_long_name("available");
    available->set_short_description("show only available groups");
    available->set_const_value("true");
    available->link_value(available_option);

    auto installed = parser.add_new_named_arg("installed");
    installed->set_long_name("installed");
    installed->set_short_description("show only installed groups");
    installed->set_const_value("true");
    installed->link_value(installed_option);

    auto hidden = parser.add_new_named_arg("hidden");
    hidden->set_long_name("hidden");
    hidden->set_short_description("show also hidden groups");
    hidden->set_const_value("true");
    hidden->link_value(hidden_option);

    patterns_to_show_options = parser.add_new_values();
    auto keys = parser.add_new_positional_arg(
        "groups_to_show",
        ArgumentParser::PositionalArg::UNLIMITED,
        parser.add_init_value(std::unique_ptr<libdnf::Option>(new libdnf::OptionString(nullptr))),
        patterns_to_show_options);
    keys->set_short_description("List of groups to show");

    auto conflict_args =
        parser.add_conflict_args_group(std::unique_ptr<std::vector<ArgumentParser::Argument *>>(
            new std::vector<ArgumentParser::Argument *>{available, installed}));

    available->set_conflict_arguments(conflict_args);
    installed->set_conflict_arguments(conflict_args);

    cmd.register_named_arg(available);
    cmd.register_named_arg(installed);
    cmd.register_named_arg(hidden);
    cmd.register_positional_arg(keys);
}


void GroupInfoCommand::run() {
    auto & ctx = static_cast<Context &>(get_session());

    ctx.base.get_rpm_package_sack()->create_system_repo(false);

    std::vector<std::string> patterns_to_show;
    if (patterns_to_show_options->size() > 0) {
        patterns_to_show.reserve(patterns_to_show_options->size());
        for (auto & pattern : *patterns_to_show_options) {
            auto option = dynamic_cast<libdnf::OptionString *>(pattern.get());
            patterns_to_show.emplace_back(option->get_value());
        }
    }

    libdnf::repo::RepoQuery enabled_repos(ctx.base);
    enabled_repos.filter_enabled(true);

    ctx.base.get_comps()->load_installed();

    using LoadFlags = libdnf::rpm::PackageSack::LoadRepoFlags;
    auto flags = LoadFlags::COMPS;
    ctx.load_rpm_repos(enabled_repos, flags);

    libdnf::comps::GroupQuery query(ctx.base.get_comps()->get_group_sack());

    // Filter by patterns if given
    if (patterns_to_show.size() > 0) {
        auto query_names = libdnf::comps::GroupQuery(query);
        query.filter_groupid(patterns_to_show, libdnf::sack::QueryCmp::IGLOB);
        query |= query_names.filter_name(patterns_to_show, libdnf::sack::QueryCmp::IGLOB);
    } else if (not hidden_option->get_value()) {
        // Filter uservisible only if patterns are not given
        query.filter_uservisible(true);
    }

    std::set<libdnf::comps::Group> group_list;

    auto query_installed = libdnf::comps::GroupQuery(query);
    query_installed.filter_installed(true);

    // --installed -> filter installed groups
    if (installed_option->get_value()) {
        group_list = query_installed.list();
    // --available / all
    } else {
        // all -> first add installed groups to the list
        if (!available_option->get_value()) {
            for (auto group: query_installed.list()) {
                group_list.emplace(group);
            }
        }
        // --available / all -> add available not-installed groups into the list
        auto query_available = libdnf::comps::GroupQuery(query);
        query_available.filter_installed(false);
        std::set<std::string> installed_groupids;
        for (auto group: query_installed.list()) {
            installed_groupids.insert(group.get_groupid());
        }
        for (auto group: query_available.list()) {
            group_list.emplace(group);
        }
    }

    for (auto group : group_list) {
        libdnf::cli::output::print_groupinfo_table(group);
        std::cout << '\n';
    }
}


}  // namespace microdnf