#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}





double flatten(double x) {
    return 2.0f / (1.0f + exp(-x)) - 1.0f;
}


int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

    int lane = 1;
    double curr_vel = 2.0;

    h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy,&lane,&curr_vel](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;


            // TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds

            // Sensor Fusion Data, a list of all other cars on the same side of the road.
            int prev_size = previous_path_x.size();

            if(prev_size > 0){
                car_s = end_path_s;
            }
            // calculate lane speeds
            vector<double> lane_speeds ={0.0,0.0,0.0};
            vector<int> lane_count={0,0,0};

            for (int i = 0; i < sensor_fusion.size(); i++) {
                float d = sensor_fusion[i][6];
                int other_lane = int(floor(d/4));
                if (other_lane < 0 || other_lane > 2)
                    continue;

                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                double check_speed = sqrt(vx*vx+vy*vy);
                lane_speeds[other_lane]+=check_speed*2.24; //convert to MPH
                lane_count[other_lane]+=1;
            }

            // calculate average speed
            for (int i = 0; i < lane_speeds.size(); i++) {
                int n = lane_count[i];
                if (n == 0){
                    lane_speeds[i]=49.5;// set to maximum allowed
                }
                    // average the speed and
                else {
                    lane_speeds[i]=lane_speeds[i]/n;
                }
            }


            bool too_close = false;

            double closest_speed = 49.5;

            // Calculate the closest Front and Back gaps
            for (int i = 0; i < sensor_fusion.size(); i++)
            {
                float d = sensor_fusion[i][6];
                //int other_lane = int(floor(d/4));
                // check if car is in the same lane as our car, d less than 8 & greater 4
                if (d < (2+4*lane+2) && d > (2+4*lane-2)){
                    // get it's velocity and displacement
                    double vx = sensor_fusion[i][3];
                    double vy = sensor_fusion[i][4];
                    double check_speed = sqrt(vx*vx + vy*vy);
                    double check_car_s = sensor_fusion[i][5];

                    check_car_s += ((double)prev_size*0.02*check_speed);

                    // see if s value is greater than ours
                    if((check_car_s > car_s) && ((check_car_s-car_s) < 30)){
                        too_close = true;
                        closest_speed = check_speed;
                    }

                }
            }

            int changing=0;

            if (too_close){
                vector<int> side_lanes;
                if (lane == 0){
                    side_lanes = {0,1};
                }
                else if (lane == 1){
                    side_lanes ={0,1,2};
                }
                else{
                    side_lanes ={1,2};
                }

                int good_lane=lane;
                double good_lane_cost=5000;
                for (int side_lane: side_lanes) {
                    double cost = 0;

                    // cost of wrong lane
                    if (side_lane != lane)
                        cost += 1000;

                    // cost of speeding
                    double avg_speed =lane_speeds[side_lane];
                    cost += flatten(2.0 * (avg_speed-49.0/avg_speed)) * 1000;

                    // cost of collision
                    vector<int> ids;
                    for (int i = 0; i < sensor_fusion.size(); i++) {
                        float other_car_d = sensor_fusion[i][6];

                        // if the car is in same lane
                        int other_car_lane = int(floor(other_car_d/4));
                        if (other_car_lane < 0 || other_car_lane > 2)
                            continue;

                        if (other_car_lane == lane){
                            ids.push_back(i);
                        }
                    }

                    // fins nearest car distance

                    double nearest = 5000;

                    for (int id : ids) {
                        double vx = sensor_fusion[id][3];
                        double vy = sensor_fusion[id][4];

                        double check_speed = sqrt(vx * vx + vy * vy); // euclidean
                        double check_start_car_s = sensor_fusion[id][5];

                        double check_end_car_s = check_start_car_s + .02*prev_size * check_speed;

                        double dist_start = abs(check_start_car_s-car_s);
                        if ( dist_start < nearest){
                            nearest = dist_start;
                        }

                        double dist_end = abs(check_end_car_s-car_s);
                        if ( dist_end < nearest){
                            nearest = dist_end;
                        }
                    }


                    double buffer = 10;

                    if (nearest < buffer)
                        cost+=pow(10,4);

                    cost += flatten(2*buffer/nearest) * 1000;

                    std::cout << "cost by " << good_lane_cost << "   " << cost<<endl;

                    if (cost < good_lane_cost) {
                        int prv_good_lane = good_lane;
                        good_lane = side_lane;
                        good_lane_cost = cost;
                        std::cout << "can change lane " << good_lane << "   " << prv_good_lane<<endl;
                        changing = 1;
                        //if(prv_good_lane != good_lane)
                        lane = good_lane;
                    }
                }
                if (good_lane == lane && (curr_vel > lane_speeds[lane] || curr_vel > closest_speed)) {
                    double dec_vel = .2 * log(curr_vel);
                    curr_vel -= dec_vel;//0.824;
                    std::cout << "decreasing by " << dec_vel << " now " << curr_vel << too_close << std::endl;
                    //too_close = false;
                }

                // switching to good lane

                lane = good_lane;
            }
            else if(curr_vel < 49.0){
                double difff = 49.0 - curr_vel;
                double inc_speed = 0.2 * (5 -  log(curr_vel));
                if ((inc_speed) > 49.0){
                    curr_vel+= difff;
                    std::cout << "increasing by "<< difff << " now " << curr_vel << too_close << std::endl;
                }else {

                    curr_vel += inc_speed;
                    std::cout << "increasing by ln "<< inc_speed << " now " << curr_vel << too_close << std::endl;
                }
            }else{
                curr_vel -= 0.124;
            }

            //   waypoints
            vector<double> ptsx;
            vector<double> ptsy;

            // reference x,y yaw states
            double ref_x = car_x;
            double ref_y = car_y;
            double ref_yaw = deg2rad(car_yaw);

            // use car as starting reference when size is less than 2
            if (prev_size < 2){
                //use two points that make the path tangent to car
                double prev_car_x = car_x - cos(car_yaw);
                double prev_car_y = car_y - sin(car_yaw);

                ptsx.push_back(prev_car_x);
                ptsx.push_back(car_x);

                ptsy.push_back(prev_car_y);
                ptsy.push_back(car_y);
            }
                //use the previous path's end points as starting reference
            else{
                //redefine reference state as previous path and point
                ref_x = previous_path_x[prev_size-1];
                ref_y = previous_path_y[prev_size-1];

                double ref_x_prev = previous_path_x[prev_size-2];
                double ref_y_prev = previous_path_y[prev_size-2];
                ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);

                //use two points that make the path tangent to the previous path's end point
                ptsx.push_back(ref_x_prev);
                ptsx.push_back(ref_x);

                ptsy.push_back(ref_y_prev);
                ptsy.push_back(ref_y);
            }

            // In Frenet coordinates, add 30-meters evenly spaced points ahead of starting reference
            double target_d = 2 + lane * 4; //lane = 1

            vector<double> next_wp0 = getXY((car_s + 30),   target_d, map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp1 = getXY((car_s + 30*2), target_d, map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp2 = getXY((car_s + 30*3), target_d, map_waypoints_s, map_waypoints_x, map_waypoints_y);

            // Add these next waypoints to Anchor Points
            ptsx.push_back(next_wp0[0]);
            ptsx.push_back(next_wp1[0]);
            ptsx.push_back(next_wp2[0]);

            ptsy.push_back(next_wp0[1]);
            ptsy.push_back(next_wp1[1]);
            ptsy.push_back(next_wp2[1]);





            //double dist_inc = 0.5;
            for (int i = 0; i < ptsx.size(); i++) {

                // shift car reference angle to 0 degree
                double shift_x = ptsx[i]-ref_x;
                double shift_y = ptsy[i]-ref_y;

                // rotate them
                ptsx[i] = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
                ptsy[i] = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));

            }

            // Create a Splineprevious_path_x.size()
            tk::spline spl;

            // set Anchor points on the Spline
            spl.set_points(ptsx, ptsy);

            vector<double> next_x_vals;
            vector<double> next_y_vals;


            // start with points from previous path
            for(int i = 0; i < prev_size; i++)
            {
                next_x_vals.push_back(previous_path_x[i]);
                next_y_vals.push_back(previous_path_y[i]);
            }

            // Calculate how to break up spline points to travel at our set reference velocity
            double target_x = 30.0;
            double target_y = spl(target_x);
            double target_dist = sqrt((target_x * target_x) + (target_y * target_y));

            double x_add_on = 0;

            // Fill up the rest of the path planner
            for(int i = 1; i <= 50-prev_size; i++)
            {

                // Calculate spacing of number of points based on reference velocity
                double N = (target_dist / (0.02 * curr_vel/2.24));//ref vel = 49
                double x_point = x_add_on + (target_x) / N;
                double y_point = spl(x_point);

                x_add_on = x_point;

                double x_ref = x_point;
                double y_ref = y_point;

                // rotate coordinates back to normal
                x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw));
                y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw));

                // add to our Reference x, y
                x_point += ref_x;
                y_point += ref_y;

                next_x_vals.push_back(x_point);
                next_y_vals.push_back(y_point);

            }


            msgJson["next_x"] = next_x_vals;
            msgJson["next_y"] = next_y_vals;












            auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
