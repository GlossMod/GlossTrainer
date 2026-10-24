includes("Scripts")
includes("BlackMythWukong")
-- The graphics host is a local, Git-ignored development tool.
if os.isfile("Tests/xmake.lua") then
    includes("Tests")
end
