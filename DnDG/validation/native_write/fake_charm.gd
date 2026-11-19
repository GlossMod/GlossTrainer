extends Node2D
signal lucky_charm_clicked
var id := -1
var x_value := -1
var effect_resource: Resource
var position_at_initialise := Vector2(INF, INF)
var navigation_interpreter := preload("res://fake_navigation.gd").new()
func initialise_lucky_charm(data: Dictionary) -> void:
	position_at_initialise = position
	id = data.id
	x_value = data.x_value
	effect_resource = preload("res://lucky_charm_effect_resources/LuckyCharmOne.gd").new()
