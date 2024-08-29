#include "ecodan.h"

namespace esphome {
namespace ecodan 
{ 
    void EcodanClimate::setup() {
        // restore all climate data, if possible
        auto restore = this->restore_state_();
        if (restore.has_value()) {
            restore->to_call(this).perform();
        }
    }

    void EcodanClimate::refresh() {
        
        bool should_publish = false;

        if (this->get_current_temp != nullptr) {
            float current_temp = this->get_current_temp();
            if (this->current_temperature != current_temp && !std::isnan(current_temp)) {
                this->current_temperature = current_temp;
                should_publish = true;
            }
        }

        // handle update from other sources than this component 
        // (after 60s to allow HP to process the value before reading back)
        auto allow_refresh = std::chrono::steady_clock::now() - this->last_update > std::chrono::seconds(60);
        if (this->get_target_temp != nullptr && allow_refresh) {
            float target_temp = this->get_target_temp();
            if (this->target_temperature != target_temp && !std::isnan(target_temp)) {
                this->target_temperature = target_temp;
                validate_target_temperature();
                should_publish = true;
            }
        }

        auto& status = this->get_status();
        if (this->dhw_climate_mode) {
            if (this->mode != climate::ClimateMode::CLIMATE_MODE_HEAT && allow_refresh) {
                this->mode = climate::ClimateMode::CLIMATE_MODE_HEAT;
                should_publish = true;
            }            
        }
        else {
            switch (status.HeatingCoolingMode) {
                case ecodan::Status::HpMode::HEAT_ROOM_TEMP:
                case ecodan::Status::HpMode::HEAT_FLOW_TEMP:
                case ecodan::Status::HpMode::HEAT_COMPENSATION_CURVE:
                    if (this->mode != climate::ClimateMode::CLIMATE_MODE_HEAT && allow_refresh) {
                        if (this->set_heating_mode != nullptr) {
                            this->last_update = std::chrono::steady_clock::now();
                            this->set_heating_mode();
                        }
                        this->mode = climate::ClimateMode::CLIMATE_MODE_HEAT;
                        should_publish = true;
                    }
                break;
                case ecodan::Status::HpMode::COOL_ROOM_TEMP:
                case ecodan::Status::HpMode::COOL_FLOW_TEMP:
                    if (this->mode != climate::ClimateMode::CLIMATE_MODE_COOL && allow_refresh) {
                        if (this->set_cooling_mode != nullptr) {
                            this->last_update = std::chrono::steady_clock::now();
                            this->set_cooling_mode();
                        }
                        this->mode = climate::ClimateMode::CLIMATE_MODE_COOL;
                        should_publish = true;
                    } 
                break;                    
            case ecodan::Status::HpMode::OFF:
                break;
            }
        }

        auto new_action = climate::CLIMATE_ACTION_IDLE;
        if (this->dhw_climate_mode) {
            if (status.Operation == Status::OperationMode::DHW_ON 
                || status.Operation == Status::OperationMode::LEGIONELLA_PREVENTION)
                new_action = climate::CLIMATE_ACTION_HEATING;
        }
        else 
        {
            switch (status.Operation)
            {
                case Status::OperationMode::HEAT_ON:
                case Status::OperationMode::FROST_PROTECT:
                        new_action = climate::CLIMATE_ACTION_HEATING;
                    break;
                case ecodan::Status::OperationMode::COOL_ON:
                        new_action = climate::CLIMATE_ACTION_COOLING;
                    break;
                case ecodan::Status::OperationMode::OFF:
                case ecodan::Status::OperationMode::DHW_ON:
                case ecodan::Status::OperationMode::LEGIONELLA_PREVENTION:
                    break;
            }
        }


        if (this->action != new_action) {
            this->action = new_action;
            should_publish = true;
        }

        if (should_publish) {
            //ESP_LOGE(TAG, "publish: %d", should_publish);
            this->publish_state();
        }        
    }

    void EcodanClimate::update() {
        refresh();
    }

    void EcodanClimate::control(const climate::ClimateCall &call) {
        
        bool should_publish = false;

        if (this->get_current_temp != nullptr) {
            this->current_temperature = this->get_current_temp();
            should_publish = true;
        }

        if (call.get_mode().has_value()) {
            // User requested mode change
            climate::ClimateMode mode = *call.get_mode();
            
            if (this->mode != mode) {
                this->mode = mode;
                switch (mode) {
                    case climate::ClimateMode::CLIMATE_MODE_HEAT:
                        if (this->set_heating_mode != nullptr)
                            this->set_heating_mode();
                    break;
                    case climate::ClimateMode::CLIMATE_MODE_COOL:
                        if (this->set_cooling_mode != nullptr)
                            this->set_cooling_mode();
                    break;                    
                    case climate::ClimateMode::CLIMATE_MODE_HEAT_COOL:
                    case climate::ClimateMode::CLIMATE_MODE_FAN_ONLY:
                    case climate::ClimateMode::CLIMATE_MODE_OFF:
                    case climate::ClimateMode::CLIMATE_MODE_DRY:
                    case climate::ClimateMode::CLIMATE_MODE_AUTO:
                    break;
                }
                // Publish updated state
                should_publish = true;
            }
        }
        

        if (call.get_target_temperature().has_value()) {
            // User requested target temperature change
            float temp = *call.get_target_temperature();
            // Send target temp to climate
            if (this->set_target_temp != nullptr && temp != this->target_temperature)
                this->set_target_temp(temp);

            this->target_temperature = temp;
            validate_target_temperature();
            should_publish = true;
        }

        if (should_publish) {
            this->last_update = std::chrono::steady_clock::now();
            this->publish_state();
        }
    }

    climate::ClimateTraits EcodanClimate::traits() {
        auto traits = climate::ClimateTraits();
        // The capabilities of the climate device
        traits.set_supports_two_point_target_temperature(false);
        traits.set_supports_current_temperature(get_current_temp != nullptr);
        traits.set_supports_action(true);
        
        if (this->dhw_climate_mode)  {

            traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
            traits.set_visual_min_temperature(40);
            traits.set_visual_max_temperature(60);
        }
        else 
        {
            traits.set_supported_modes({climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_COOL});
            
            // dynamic adjustment in esphome works, but does not seem to progagate to HA
            // auto is_cooling = this->mode == climate::ClimateMode::CLIMATE_MODE_COOL;
            // traits.set_visual_min_temperature(is_cooling ? 5 : 25);
            // traits.set_visual_max_temperature(is_cooling ? 20 : 60);
            traits.set_visual_min_temperature(15);
            traits.set_visual_max_temperature(25);           
            //ESP_LOGE(TAG, "min: %f, max: %f", traits.get_visual_min_temperature(), traits.get_visual_max_temperature());
        }
        
        traits.set_visual_target_temperature_step(1);
        traits.set_visual_current_temperature_step(1);

        return traits;
    }

    void EcodanClimate::validate_target_temperature() {
        if (std::isnan(this->target_temperature)) {
            this->target_temperature =
                ((this->get_traits().get_visual_max_temperature() - this->get_traits().get_visual_min_temperature()) / 2) +
                this->get_traits().get_visual_min_temperature();
        } else {
            // target_temperature must be between the visual minimum and the visual maximum
            if (this->target_temperature < this->get_traits().get_visual_min_temperature())
                this->target_temperature = this->get_traits().get_visual_min_temperature();

            if (this->target_temperature > this->get_traits().get_visual_max_temperature())
                this->target_temperature = this->get_traits().get_visual_max_temperature();
        }
    }

} // namespace ecodan
} // namespace esphome
