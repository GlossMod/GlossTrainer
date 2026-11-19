extends Node2D
signal relic_clicked
var this_relic_id := ""
var relic_effect_resource: Resource
var navigation_interpreter := preload("res://fake_navigation.gd").new()
func set_relic_art(id: String) -> void:
	this_relic_id = id
func set_relic_effect_resource(_id: String) -> void:
	relic_effect_resource = preload("res://lucky_charm_effect_resources/LuckyCharmOne.gd").new()
