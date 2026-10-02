#include "../include/antworld.h"
#include <vector>
#include <cmath>

int phase = 0;
bool setup_done = false;

const AntWorld *last_world = nullptr;

std::vector<Coord> scan_points;
std::vector<Coord> food_found;

int next_point = 0;
int ant_num = 0;
int stuck_count = 0;

struct FoodInfo
{
    Coord spot; int cost;
};

// reset for a new game
void reset_strategy(void)
{
    phase = 0;
    setup_done = false;
    scan_points.clear();
    food_found.clear();
    next_point = 0;
    ant_num = 0;
    stuck_count = 0;
}

// Finds the lowest energy cost between two spots
// Returns a large number if there is no path
int path_cost(MapTemplate &terrain_map, Coord start, Coord end)
{
    if (start == end)
        return 0;

    std::vector<Coord> path = shortestPath(terrain_map, start, end);

    if (path.empty())
        return 1000000;

    return calculatePathCost(terrain_map, path);
}

// save new food
void add_food(Coord food)
{
    for (int i = 0; i < (int)food_found.size(); i++)
    {
        if (food_found[i] == food)
            return;
    }

    food_found.push_back(food);
}

// remove food after it is picked up
void remove_food(Coord food)
{
    for (int i = 0; i < (int)food_found.size(); i++)
    {
        if (food_found[i] == food)
        {
            food_found.erase(food_found.begin() + i);
            return;
        }
    }
}

// Saves everything found from one scan
void save_food(std::vector<Coord> food)
{
    for (int i = 0; i < (int)food.size(); i++)
        add_food(food[i]);
}

// distance without terrain cost
int grid_distance(Coord a, Coord b)
{
    return std::abs(a.first - b.first) + std::abs(a.second - b.second);
}

// Makes enough scan points to cover the map.
// The points are then ordered starting from home.
void make_scan_points(int rows, int cols, int radius, Coord home)
{
    std::vector<int> scan_rows;
    std::vector<int> scan_cols;

    int spacing = 2 * radius + 1;

    for (int row = radius; row < rows; row += spacing)
        scan_rows.push_back(row);

    if (scan_rows.empty())
        scan_rows.push_back(rows / 2);
    else if (scan_rows.back() + radius < rows - 1)
        scan_rows.push_back(rows - 1);

    for (int col = radius; col < cols; col += spacing)
        scan_cols.push_back(col);

    if (scan_cols.empty())
        scan_cols.push_back(cols / 2);
    else if (scan_cols.back() + radius < cols - 1)
        scan_cols.push_back(cols - 1);

    for (int row = 0; row < (int)scan_rows.size(); row++)
    {
        for (int col = 0; col < (int)scan_cols.size(); col++)
            scan_points.push_back(Coord(scan_rows[row], scan_cols[col]));
    }


    std::vector<Coord> ordered;
    Coord current = home;


    while (!scan_points.empty())
    {
        int best = 0;
        int best_distance = grid_distance(current, scan_points[0]);

        for (int i = 1; i < (int)scan_points.size(); i++)
        {
            int distance = grid_distance(current, scan_points[i]);

            if (distance < best_distance)
            {
                best = i;
                best_distance = distance;
            }
        }

        ordered.push_back(scan_points[best]);
        current = scan_points[best];

        scan_points.erase(scan_points.begin() + best);
    }

    scan_points = ordered;
}

// Sorts the saved food from cheapest to most expensive energy usage
void sort_food(MapTemplate &terrain_map, Coord home, std::vector<FoodInfo> &food_list)
{
    food_list.clear();

    for (int i = 0; i < (int)food_found.size(); i++)
    {
        FoodInfo food;

        food.spot = food_found[i];
        food.cost = path_cost(terrain_map, home, food_found[i]);

        food_list.push_back(food);
    }

    for (int pass = 0; pass < (int)food_list.size() - 1; pass++)
    {
        for (int i = 0; i < (int)food_list.size() - 1 - pass; i++)
        {
            if (food_list[i].cost > food_list[i + 1].cost)
            {
                FoodInfo temp = food_list[i];

                food_list[i] = food_list[i + 1];
                food_list[i + 1] = temp;
            }
        }
    }
}




// Checks if an ant can die while moving food closer to home. This is used when it cannot make the full trip.
bool can_relay(MapTemplate &terrain_map, Coord food, Coord home, int energy_left)
{
    std::vector<Coord> path = shortestPath(terrain_map, food, home);

    for (int i = 1; i < (int)path.size(); i++)
    {
        std::vector<Coord> part_path;

        for (int j = 0; j <= i; j++)
            part_path.push_back(path[j]);

        int cost = calculatePathCost(terrain_map, part_path);

        if (cost == energy_left && path[i] != home)
            return true;

        if (cost > energy_left)
            return false;
    }

    return false;
}


// Uses leftover energy from ants after gathering is done
void use_energy(Ant &ant, MapTemplate &terrain_map, MapTemplate &food_map)
{
    int row = ant.position.first;
    int col = ant.position.second;

    int dr[4] = {-1, 1, 0, 0};
    int dc[4] = {0, 0, -1, 1};

    Coord next = ant.position;
    int best_cost = 1000000;

    for (int i = 0; i < 4; i++)
    {
        int new_row = row + dr[i];
        int new_col = col + dc[i];

        if (new_row < 0 || new_row >= (int)terrain_map.size())
            continue;

        if (new_col < 0 || new_col >= (int)terrain_map[0].size())
            continue;

        int cost = 1 + std::abs(terrain_map[row][col] - terrain_map[new_row][new_col]);

        if (cost <= ant.energy)
        {
            if (cost == ant.energy)
            {
                next = Coord(new_row, new_col);
                break;
            }

            if (cost < best_cost)
            {
                best_cost = cost;
                next = Coord(new_row, new_col);
            }
        }
    }

    if (next != ant.position)
        ant.move(terrain_map, next, food_map);
}







// *Main ant strategy*
void AntWorld::forage()
{
    if (ants.empty())
        return;

    // new game
    if (last_world != this)
    {
        reset_strategy();
        last_world = this;
    }

    // run once at the start
    if (!setup_done)
    {
        int rows = foodMap.size();
        int cols = foodMap[0].size();
        int radius = ants[0].foodRadius;

        make_scan_points(rows, cols, radius, homeCoordinates);
        save_food(ants[0].foodScan(foodMap));

        setup_done = true;
    }

    if (ant_num >= (int)ants.size())
        ant_num = 0;

    // =========================
    //      SCOUTING!!!!
    // =========================

    if (phase == 0)
    {
        Ant &ant = ants[ant_num];

        // A scan point can have food on it, so move() may pick it up by accident.
        if (ant.carryingFood)
        {
            ant.returnHome(terrainMap, foodMap);

            if (ant.energy == 0 && ant.position != homeCoordinates)
                add_food(ant.position);

            return;
        }

        save_food(ant.foodScan(foodMap));

        // all scan points done
        if (next_point >= (int)scan_points.size())
        {
            phase = 1;
            ant_num = 0;
            stuck_count = 0;

            return;
        }

        Coord destination = scan_points[next_point];

        // keep scouting until this ant cannot move anymore
        Coord old_position = ant.position;
        Coord final_position = ant.move(terrainMap, destination, foodMap);

        if (final_position == destination)
        {
            next_point++;

            save_food(ant.foodScan(foodMap));

            if (ant.carryingFood)
                remove_food(destination);

            stuck_count = 0;
        }
        else if (final_position == old_position)
        {
            ant_num = (ant_num + 1) % ants.size();
            stuck_count++;
        }

        return;
    }









    // =========================
    // GATHERING
    // =========================


    if (phase == 1)
    {
        if (food_found.empty())
        {
            phase = 2;
            return;
        }

        Ant &ant = ants[ant_num];

        // finish the food already being carried
        if (ant.carryingFood)
        {
            ant.returnHome(terrainMap, foodMap);

            if (ant.energy == 0 && ant.position != homeCoordinates)
                add_food(ant.position);

            return;
        }

        // gathering starts from home
        if (ant.position != homeCoordinates)
        {
            ant.returnHome(terrainMap, foodMap);

            if (ant.position != homeCoordinates)
            {
                ant_num = (ant_num + 1) % ants.size();
                stuck_count++;
            }

            return;
        }

        std::vector<FoodInfo> food_list;

        sort_food(terrainMap, homeCoordinates, food_list);

        bool moved_food = false;

        for (int i = 0; i < (int)food_list.size(); i++)
        {
            Coord food = food_list[i].spot;

            int to_food = food_list[i].cost;
            int to_home = path_cost(terrainMap, food, homeCoordinates);

            // Everything after this costs even more.
            if (to_food > ant.energy)
                break;





            // full trip
            if (to_food + to_home <= ant.energy)
            {
                ant.move(terrainMap, food, foodMap);

                if (ant.carryingFood)
                {
                    remove_food(food);
                    ant.returnHome(terrainMap, foodMap);
                }
                else
                {
                    remove_food(food);
                }

                moved_food = true;
                break;
            }

            // try a relay
            int energy_left = ant.energy - to_food;

            if (can_relay(terrainMap, food, homeCoordinates, energy_left))
            {
                ant.move(terrainMap, food, foodMap);

                if (ant.carryingFood)
                {
                    remove_food(food);

                    ant.returnHome(terrainMap, foodMap);

                    if (ant.energy == 0 && ant.position != homeCoordinates)
                        add_food(ant.position);
                }
                else
                {
                    remove_food(food);
                }

                moved_food = true;
                break;
            }
        }

        if (moved_food)
        {
            stuck_count = 0;
            return;
        }

        // try the next ant
        ant_num = (ant_num + 1) % ants.size();
        stuck_count++;

        if (stuck_count >= (int)ants.size())
            phase = 2;

        return;
    }













    // =========================
    // the END
    // =========================

    if (phase == 2)
    {
        use_energy(ants[ant_num], terrainMap, foodMap);

        ant_num++;

        if (ant_num >= (int)ants.size())
            ant_num = 0;

        return;
    }
}