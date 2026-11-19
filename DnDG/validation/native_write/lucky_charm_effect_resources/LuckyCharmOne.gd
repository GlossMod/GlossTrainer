extends Resource
var effects_list: Array = ["clicked_conditional_effect", "clicked_effect"]
var effect_in_progress := false
var touch_calls := 0
func conditionally_set_can_be_clicked(_charm: Node) -> void:
	touch_calls += 1
